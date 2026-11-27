// One Cordis service in the compiler host. The LSP worker serializes
// selection and requests, and each analysis holds its provider generation.
#pragma once

#include "cordis/diagnostics.h"
#include "cordis/graph.h"
#include "cordis/service.h"

namespace klang {

struct CordisDiagnosticsContract {
    using Api = k_cordis_diagnostics_v1_api;
    static constexpr const char *name = "diagnostics";
    static constexpr const char *entry = K_CORDIS_DIAGNOSTICS_ENTRY;
    static constexpr uint32_t version = K_CORDIS_DIAGNOSTICS_ABI;
    static bool Supports(const Api &api) { return api.inspect != nullptr; }
};
using CordisDiagnosticService = cordis::Service<CordisDiagnosticsContract>;
using CordisDiagnosticProvider = CordisDiagnosticService::Generation;

class CordisDiagnostics {
    CordisDiagnosticService service;
    cordis::Catalog catalog;
    optional<cordis::Graph> graph;

    static k_cordis_text Text(string_view text) { return { text.data(), text.size() }; }

    void Activate(cordis::Graph resolved) {
        auto &provider = resolved.providers.at("diagnostics");
        if (resolved.providers.size() > 1)
            throw cordis::GraphError { { "the language server can only activate the diagnostics service" },
                                      "unsupported_service", { cat("diagnostics=", provider.id) } };
        if (provider.linked) {
            service.Reset();
        } else {
            service.RegisterLibrary(provider.id, provider.artifact);
            try {
                service.Select(provider.id);
            } catch (const cordis::AbiError &e) {
                throw cordis::GraphError { e, "incompatible_abi", { cat("diagnostics=", provider.id) } };
            } catch (const CompileError &e) {
                throw cordis::GraphError { e, "activation_failed", { cat("diagnostics=", provider.id) } };
            }
        }
        graph = std::move(resolved);
    }

public:
    static vector<Diagnostic> Inspect(const CordisDiagnosticProvider &provider, const string &root,
                                      const Ast &ast, span<const Diagnostic> diags) {
        vector<k_cordis_source_v1> files;
        files.reserve(ast.sources.size());
        for (auto &[path, contents] : ast.sources) files.push_back({ Text(path), Text(*contents) });
        vector<k_cordis_diagnostic_v1> findings;
        findings.reserve(diags.size());
        for (auto &d : diags) {
            auto &w = d.where;
            findings.push_back({ w.fileidx, w.line, w.col, w.endline, w.endcol,
                                 (uint32_t)(d.warning ? K_CORDIS_WARNING : K_CORDIS_ERROR), Text(d.message) });
        }
        k_cordis_snapshot_v1 snapshot { Text(root), files.data(), files.size(),
                                       findings.data(), findings.size() };
        struct Sink {
            size_t files;
            vector<Diagnostic> additions;
        } sink { files.size() };
        auto emit = [](void *context, const k_cordis_diagnostic_v1 *d) -> int32_t {
            auto &out = *static_cast<Sink *>(context);
            if (!d || d->file_index < 0 || (size_t)d->file_index >= out.files ||
                d->line <= 0 || d->column < 0 || d->end_line < 0 || d->end_column < 0 ||
                (d->end_line > 0 && (d->column == 0 || d->end_column == 0 ||
                    pair(d->end_line, d->end_column) < pair(d->line, d->column))) ||
                (d->end_line == 0 && d->end_column != 0) ||
                (d->severity != K_CORDIS_WARNING && d->severity != K_CORDIS_ERROR) ||
                !d->message.data || d->message.size == 0 ||
                d->message.size > string().max_size()) return 0;
            Diagnostic added;
            added.warning = d->severity == K_CORDIS_WARNING;
            added.where = { d->file_index, d->line, d->column, d->end_line, d->end_column };
            added.message.assign(d->message.data, (size_t)d->message.size);
            out.additions.push_back(std::move(added));
            return 1;
        };
        provider.Call<&k_cordis_diagnostics_v1_api::inspect>(&snapshot, emit, &sink);
        return std::move(sink.additions);
    }

    static cordis::Catalog ReadCatalog(const string &path, const string &exe) {
        const cordis::LinkedProvider linked[] = { { "builtin", "diagnostics", 1, exe } };
        return cordis::Catalog::Load(path, linked);
    }

    // Clients select identifiers from the trusted startup manifest.
    static CordisDiagnostics Load(const string &path, const string &exe, const string &lock = {}) {
        CordisDiagnostics registry;
        registry.catalog = ReadCatalog(path, exe);
        auto resolved = registry.catalog.Resolve("diagnostics");
        if (!lock.empty()) resolved.VerifyLock(lock);
        registry.Activate(std::move(resolved));
        return registry;
    }

    bool Enabled() const { return catalog.Enabled(); }
    const optional<cordis::Graph> &ActiveGraph() const { return graph; }
    string ActiveId() const {
        auto active = Pin();
        return active ? active->id : "builtin";
    }
    shared_ptr<const CordisDiagnosticProvider> Pin() const { return service.Pin(); }
    void Reset() { service.Reset(); graph.reset(); }

    bool Select(const string &id) {
        if (graph && ActiveId() == id) return false;
        Activate(catalog.Resolve("diagnostics", id));
        return true;
    }
};

}  // namespace klang
