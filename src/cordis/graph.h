// A provider catalog resolves an immutable, content-addressed graph before a host loads native code.
#pragma once

#include "../includes.h"
#include "../utils.h"

namespace klang::cordis {

struct GraphError : CompileError {
    string code;
    vector<string> chain;
    vector<string> candidates;

    string Data() const;
};

struct VersionRange {
    uint32_t min = 1;
    uint32_t max = UINT32_MAX;

    bool Contains(uint32_t value) const { return value >= min && value <= max; }
};

struct LinkedProvider {
    string id;
    string service;
    uint32_t version;
    string executable;
};

struct ResolvedInput {
    string path;
    string digest;
};

struct ResolvedProvider {
    string id;
    uint32_t version;
    bool linked;
    string artifact;
    string digest;
    set<string> capabilities;
    map<string, VersionRange> requirements;
    map<string, ResolvedInput> inputs;
};

struct Graph {
    string root;
    set<string> grants;
    map<string, ResolvedProvider> providers;  // Keyed by service.
    string identity;
    vector<string> activation_order;  // Dependencies precede their consumers.

    string Document() const;
    vector<string> DependencyChain(const string &service) const;
    void VerifyLock(const string &path) const;
    void VerifyInputs() const;
};

class Catalog {
    struct Provider {
        string service;
        uint32_t version = 1;
        bool linked = false;
        string artifact;
        set<string> capabilities;
        map<string, VersionRange> requirements;
        map<string, string> inputs;
    };

    string path;
    map<string, Provider> providers;
    map<string, string> selections;
    set<string> grants;
    int version = 0;

public:
    static Catalog Load(const string &path, span<const LinkedProvider> linked);
    bool Enabled() const { return version != 0; }
    Graph Resolve(const string &service, const optional<string> &selection = {}) const;
};

}  // namespace klang::cordis
