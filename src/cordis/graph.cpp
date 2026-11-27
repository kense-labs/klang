#include "graph.h"

#include "../content_digest.h"
#include "../json.h"
#include "../paths.h"

namespace klang::cordis {
namespace {

[[noreturn]] void Fail(string code, const vector<string> &chain, string message,
                       vector<string> candidates = {}) {
    if (!chain.empty()) {
        message += " [";
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i) message += " -> ";
            message += chain[i];
        }
        message += ']';
    }
    throw GraphError { { std::move(message) }, std::move(code), chain, std::move(candidates) };
}

[[noreturn]] void Invalid(string message) { Fail("invalid_manifest", {}, std::move(message)); }

void Object(const Json &value, const string &where, std::initializer_list<string_view> fields = {}) {
    if (value.kind != Json::OBJECT) Invalid(cat(where, " must be an object"));
    set<string> keys;
    for (auto &[key, unused] : value.obj) {
        if (!keys.insert(key).second) Invalid(cat("duplicate field ", key, " in ", where));
        if (fields.size() && std::find(fields.begin(), fields.end(), key) == fields.end())
            Invalid(cat("unknown field ", key, " in ", where));
    }
}

bool IsText(const string &value) { return !value.empty() && value.find('\0') == string::npos; }

bool IsName(const string &name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-';
    });
}

string TextField(const Json *value, const string &where) {
    if (!value || value->kind != Json::STRING || !IsText(value->str))
        Invalid(cat(where, " must be a nonempty string"));
    return value->str;
}

string NameField(const Json *value, const string &where) {
    auto name = TextField(value, where);
    if (!IsName(name)) Invalid(cat("invalid name in ", where));
    return name;
}

uint32_t VersionField(const Json *value, const string &where) {
    if (!value || value->kind != Json::NUMBER || !(value->num >= 1 && value->num <= UINT32_MAX) ||
        floor(value->num) != value->num)
        Invalid(cat(where, " must be a positive 32-bit integer"));
    return static_cast<uint32_t>(value->num);
}

set<string> Names(const Json *value, const string &where) {
    set<string> names;
    if (!value) return names;
    if (value->kind != Json::ARRAY) Invalid(cat(where, " must be an array of names"));
    for (auto &entry : value->arr)
        if (!names.insert(NameField(&entry, where)).second) Invalid(cat("duplicate name in ", where));
    return names;
}

string ArtifactPath(const filesystem::path &path) {
    return filesystem::path(AbsolutePath(path.string())).generic_string();
}

string ArtifactKey(string path, bool follow_symlinks) {
    std::error_code error;
    if (follow_symlinks) {
        auto canonical = filesystem::weakly_canonical(path, error);
        if (!error) path = canonical.generic_string();
    }
#ifdef _WIN32
    for (auto &c : path) c = (char)tolower((unsigned char)c);
#endif
    return path;
}

void Strings(JsonOut &out, const auto &values) {
    out.BeginArray();
    for (auto &value : values) out.Str(value);
    out.EndArray();
}

string GraphBody(const Graph &graph) {
    JsonOut out;
    out.BeginObject();
    out.Member("version", 1);
    out.Member("root", graph.root);
    out.Key("grants");
    Strings(out, graph.grants);
    out.Key("nodes");
    out.BeginArray();
    for (auto &[service, provider] : graph.providers) {
        out.BeginObject();
        out.Member("service", service);
        out.Member("provider", provider.id);
        out.Member("version", (int64_t)provider.version);
        out.Member("kind", provider.linked ? "linked" : "dynamic");
        out.Member("sha256", provider.digest);
        out.Key("capabilities");
        Strings(out, provider.capabilities);
        out.Key("inputs");
        out.BeginObject();
        for (auto &[name, input] : provider.inputs) out.Member(name, input.digest);
        out.EndObject();
        out.EndObject();
    }
    out.EndArray();
    out.Key("edges");
    out.BeginArray();
    for (auto &[service, provider] : graph.providers) {
        for (auto &[required, range] : provider.requirements) {
            out.BeginObject();
            out.Member("from", service);
            out.Member("to", required);
            out.Member("minVersion", (int64_t)range.min);
            out.Member("maxVersion", (int64_t)range.max);
            out.EndObject();
        }
    }
    out.EndArray();
    out.EndObject();
    return std::move(out.s);
}

// The lock is the canonical graph document with optional formatting whitespace.
// Object order is part of the versioned serialization used by the digest.
void WriteLockValue(JsonOut &out, const Json &value) {
    switch (value.kind) {
        case Json::STRING: out.Str(value.str); return;
        case Json::NUMBER:
            if (value.num >= 1 && value.num <= UINT32_MAX && floor(value.num) == value.num) {
                out.Int((int64_t)value.num);
                return;
            }
            break;
        case Json::ARRAY:
            out.BeginArray();
            for (auto &entry : value.arr) WriteLockValue(out, entry);
            out.EndArray();
            return;
        case Json::OBJECT: {
            Object(value, "Cordis lock");
            out.BeginObject();
            for (auto &[key, entry] : value.obj) {
                out.Key(key);
                WriteLockValue(out, entry);
            }
            out.EndObject();
            return;
        }
        default: break;
    }
    Invalid("unexpected value in Cordis lock");
}

}  // namespace

vector<string> Graph::DependencyChain(const string &service) const {
    map<string, string> parent { { root, {} } };
    deque<string> pending { root };
    while (!pending.empty() && !parent.count(service)) {
        auto current = std::move(pending.front());
        pending.pop_front();
        for (auto &[dependency, unused] : providers.at(current).requirements)
            if (parent.emplace(dependency, current).second) pending.push_back(dependency);
    }
    vector<string> chain;
    for (auto current = parent.count(service) ? service : root; !current.empty(); current = parent.at(current))
        chain.push_back(cat(current, "=", providers.at(current).id));
    reverse(chain.begin(), chain.end());
    return chain;
}

string GraphError::Data() const {
    JsonOut out;
    out.BeginObject();
    out.Member("code", code);
    out.Member("message", msg);
    out.Key("chain");
    Strings(out, chain);
    out.Key("candidates");
    Strings(out, candidates);
    out.EndObject();
    return std::move(out.s);
}

string Graph::Document() const {
    JsonOut out;
    out.BeginObject();
    out.Member("identity", identity);
    out.Key("graph");
    out.Raw(GraphBody(*this));
    out.EndObject();
    return std::move(out.s);
}

void Graph::VerifyLock(const string &path) const {
    string text;
    std::error_code error;
    if (!filesystem::is_regular_file(path, error) || !LoadFile(path, text))
        Fail("unreadable_lock", {}, cat("cannot read Cordis lock ", path));
    Json lock;
    const Json *locked_graph;
    string locked_identity;
    try {
        if (text.find('\0') != string::npos) Invalid("NUL in Cordis lock");
        lock = ParseJson(text);
        Object(lock, "Cordis lock", { "identity", "graph" });
        locked_identity = TextField(lock.Get("identity"), "lock identity");
        locked_graph = lock.Get("graph");
        if (!locked_graph || locked_graph->kind != Json::OBJECT ||
            VersionField(locked_graph->Get("version"), "lock graph version") != 1 ||
            locked_graph->Str("root") != root)
            Invalid(cat("expected a version 1 lock graph rooted at ", root));
        JsonOut canonical;
        WriteLockValue(canonical, *locked_graph);
        if (locked_identity != cat("sha256:", Sha256(canonical.s))) Invalid("lock graph digest does not match");
    } catch (const CompileError &e) {
        Fail("invalid_lock", {}, cat("invalid Cordis lock ", path, ": ", e.msg));
    }
    if (locked_identity == identity) return;

    auto current_graph = ParseJson(GraphBody(*this));
    map<string, const Json *> old_nodes;
    for (auto &node : locked_graph->Arr("nodes")) old_nodes.emplace(node.Str("service"), &node);
    for (auto &node : current_graph.Arr("nodes")) {
        auto service = node.Str("service");
        auto old = old_nodes.find(service);
        auto chain = [&] { return DependencyChain(service); };
        if (old != old_nodes.end() && old->second->Str("provider") == node.Str("provider")) {
            auto old_inputs = old->second->Get("inputs");
            for (auto &[name, digest] : node.Get("inputs")->obj)
                if (!old_inputs || old_inputs->Str(name) != digest.str)
                    Fail("stale_lock", chain(), cat("Cordis tracked input ", name, " differs from ", path));
            if (old->second->Str("sha256") != node.Str("sha256"))
                Fail("stale_lock", chain(), cat("Cordis provider artifact differs from ", path));
        }
        JsonOut actual, expected;
        WriteLockValue(actual, node);
        if (old != old_nodes.end()) WriteLockValue(expected, *old->second);
        if (actual.s != expected.s)
            Fail("stale_lock", chain(), cat("Cordis provider selection differs from ", path));
    }
    Fail("stale_lock", DependencyChain(root), cat("Cordis graph differs from ", path));
}

void Graph::VerifyInputs() const {
    map<string, optional<string>> digests;
    for (auto &[service, provider] : providers) {
        auto compare = [&](const string &path, const string &expected, const string &label) {
            auto [it, inserted] = digests.try_emplace(path);
            if (inserted) it->second = Sha256File(path);
            if (it->second != expected)
                Fail("changed_input", DependencyChain(service), cat(label, " changed after Cordis resolution: ", path));
        };
        if (!provider.linked) compare(provider.artifact, provider.digest, "provider artifact");
        for (auto &[name, input] : provider.inputs)
            compare(input.path, input.digest, cat("tracked input ", name));
    }
}

Catalog Catalog::Load(const string &path, span<const LinkedProvider> linked) {
    Catalog catalog;
    catalog.path = ArtifactPath(path);
    auto manifest = filesystem::path(catalog.path);
    auto dir = manifest.parent_path();
    string text;
    std::error_code error;
    if (!filesystem::is_regular_file(manifest, error) || !LoadFile(catalog.path, text))
        Fail("unreadable_manifest", {}, cat("cannot read Cordis manifest ", catalog.path));
    Json config;
    try {
        if (text.find('\0') != string::npos) Invalid("NUL in Cordis manifest");
        config = ParseJson(text);
        Object(config, "manifest");
        auto version = VersionField(config.Get("version"), "manifest version");
        if (version != 1 && version != 2) Invalid("expected Cordis manifest version 1 or 2");
        catalog.version = static_cast<int>(version);

        for (auto &provider : linked) {
            if (!IsName(provider.id) || !IsName(provider.service) || !provider.version ||
                provider.executable.empty() ||
                !catalog.providers.emplace(provider.id, Provider { provider.service, provider.version, true,
                                                                   ArtifactPath(provider.executable) }).second)
                Invalid(cat("invalid linked Cordis provider ", provider.id));
        }

        auto entries = config.Get("providers");
        if (!entries) Invalid("expected a providers object");
        Object(*entries, "providers");
        set<string> artifact_paths;
        for (auto &[id, entry] : entries->obj) {
            if (!IsText(id) || catalog.providers.count(id) || (version == 2 && !IsName(id)))
                Invalid(cat("invalid or reserved Cordis provider ", id));
            auto where = cat("provider ", id);
            Provider provider;
            if (version == 1) {
                provider.service = "diagnostics";
                provider.artifact = ArtifactPath(dir / TextField(&entry, where));
            } else {
                Object(entry, where, { "service", "version", "library", "capabilities", "requires", "inputs" });
                provider.service = NameField(entry.Get("service"), cat(where, " service"));
                provider.version = VersionField(entry.Get("version"), cat(where, " version"));
                provider.artifact = ArtifactPath(dir / TextField(entry.Get("library"), cat(where, " library")));
                provider.capabilities = Names(entry.Get("capabilities"), cat(where, " capabilities"));
                if (auto requires_by_service = entry.Get("requires")) {
                    Object(*requires_by_service, cat(where, " requires"));
                    for (auto &[service, bounds] : requires_by_service->obj) {
                        if (!IsName(service)) Invalid(cat("invalid required service in ", where));
                        Object(bounds, cat(where, " requires ", service), { "minVersion", "maxVersion" });
                        VersionRange range;
                        if (auto min = bounds.Get("minVersion")) range.min = VersionField(min, "minVersion");
                        if (auto max = bounds.Get("maxVersion")) range.max = VersionField(max, "maxVersion");
                        if (range.min > range.max) Invalid(cat("inverted version bounds for ", where, " -> ", service));
                        provider.requirements.emplace(service, range);
                    }
                }
                if (auto inputs = entry.Get("inputs")) {
                    Object(*inputs, cat(where, " inputs"));
                    for (auto &[name, path] : inputs->obj) {
                        if (!IsName(name)) Invalid(cat("invalid input name in ", where));
                        provider.inputs.emplace(name, ArtifactPath(dir / TextField(&path, cat(where, " input ", name))));
                    }
                }
            }
            if (!artifact_paths.insert(ArtifactKey(provider.artifact, version == 2)).second)
                Invalid("Cordis provider artifact paths must be distinct");
            catalog.providers.emplace(id, std::move(provider));
        }

        if (version == 1) {
            auto selection = config.Get("diagnostics");
            catalog.selections["diagnostics"] = selection ? TextField(selection, "diagnostics selection") : "builtin";
        } else {
            Object(config, "manifest", { "version", "providers", "selections", "grants" });
            catalog.grants = Names(config.Get("grants"), "manifest grants");
            if (auto selections = config.Get("selections")) {
                Object(*selections, "selections");
                for (auto &[service, id] : selections->obj) {
                    if (!IsName(service)) Invalid("invalid service name in selections");
                    catalog.selections.emplace(service, NameField(&id, cat("selection for ", service)));
                }
            }
        }
    } catch (const GraphError &e) {
        Fail(e.code, e.chain, cat(e.msg, " in ", catalog.path));
    } catch (const CompileError &e) {
        Fail("invalid_manifest", {}, cat(e.msg, " in ", catalog.path));
    }
    return catalog;
}

Graph Catalog::Resolve(const string &service, const optional<string> &selection) const {
    if (!Enabled()) Fail("no_manifest", {}, "Cordis requires a manifest");
    Graph graph { service, grants };
    using Requirements = map<string, VersionRange>;
    struct Frame {
        string service;
        Requirements::const_iterator next;
        Requirements::const_iterator end;
    };
    map<string, vector<decltype(providers)::const_iterator>> by_service;
    for (auto it = providers.begin(); it != providers.end(); ++it)
        by_service[it->second.service].push_back(it);
    vector<Frame> stack;
    set<string> visiting;
    map<string, string> parent;
    vector<string> chain;

    auto schedule = [&](const string &name, const VersionRange &range) {
        chain.push_back(name);
        const auto *override_id = name == service && selection ? &*selection : nullptr;
        auto explicit_id = selections.find(name);
        if (!override_id && explicit_id != selections.end()) override_id = &explicit_id->second;
        auto selected = providers.end();
        if (override_id) {
            selected = providers.find(*override_id);
            chain.back() = cat(name, "=", *override_id);
            if (selected == providers.end() || selected->second.service != name)
                Fail("missing_provider", chain, cat("no Cordis provider ", *override_id, " for ", name));
            if (!range.Contains(selected->second.version))
                Fail("incompatible_version", chain, cat("Cordis ", name, " requires version ", range.min,
                     " through ", range.max, ", but ", *override_id, " provides ", selected->second.version));
        } else {
            const auto choices = by_service.find(name);
            if (choices == by_service.end())
                Fail("missing_provider", chain, cat("no Cordis provider for ", name));
            vector<string> eligible;
            for (auto it : choices->second) {
                if (!range.Contains(it->second.version)) continue;
                eligible.push_back(it->first);
                selected = it;
            }
            if (eligible.empty()) Fail("incompatible_version", chain, cat("no Cordis ", name,
                                       " provider in version range ", range.min, " through ", range.max));
            if (eligible.size() > 1)
                Fail("ambiguous_provider", chain, cat("select a Cordis provider for ", name), std::move(eligible));
            chain.back() = cat(name, "=", selected->first);
        }

        auto &[id, provider] = *selected;
        if (auto previous = graph.providers.find(name); previous != graph.providers.end()) {
            if (previous->second.id != id)
                Fail("conflicting_provider", chain, cat("Cordis ", name, " already resolves to ", previous->second.id),
                     { previous->second.id, id });
            if (visiting.count(name)) Fail("dependency_cycle", chain, "Cordis dependency cycle");
            chain.pop_back();
            return;
        }
        for (auto &capability : provider.capabilities)
            if (!grants.count(capability))
                Fail("missing_capability", chain, cat("Cordis provider ", id, " requires grant ", capability));

        graph.providers.emplace(name, ResolvedProvider { id, provider.version, provider.linked,
                                                         provider.artifact, {}, provider.capabilities,
                                                         provider.requirements, {} });
        parent.emplace(name, stack.empty() ? "" : stack.back().service);
        visiting.insert(name);
        stack.push_back({ name, provider.requirements.begin(), provider.requirements.end() });
    };

    schedule(service, {});
    while (!stack.empty()) {
        auto &frame = stack.back();
        if (frame.next == frame.end) {
            graph.activation_order.push_back(frame.service);
            visiting.erase(frame.service);
            stack.pop_back();
            chain.pop_back();
            continue;
        }
        auto &[required, range] = *frame.next++;
        schedule(required, range);
    }

    // Check the complete graph before reading any artifact or loading code.
    map<string, string> digests;
    for (auto &[name, provider] : graph.providers) {
        auto digest = [&](const string &path) -> const string & {
            auto [it, inserted] = digests.try_emplace(path);
            if (inserted) {
                auto hash = Sha256File(path);
                if (!hash) {
                    vector<string> source;
                    for (auto node = name; !node.empty(); node = parent.at(node))
                        source.push_back(cat(node, "=", graph.providers.at(node).id));
                    reverse(source.begin(), source.end());
                    Fail("unreadable_input", source, cat("cannot read Cordis input ", path));
                }
                it->second = std::move(*hash);
            }
            return it->second;
        };
        provider.digest = digest(provider.artifact);
        for (auto &[input, path] : providers.at(provider.id).inputs)
            provider.inputs.emplace(input, ResolvedInput { path, digest(path) });
    }
    graph.identity = cat("sha256:", Sha256(GraphBody(graph)));
    return graph;
}

}  // namespace klang::cordis
