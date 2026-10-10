"use strict";
/* MiniGit sandbox: a browser port of the C++ modules in module/src.
   Output strings, argument rules and data structures follow the real CLI. */

// ---------------------------------------------------------------------------
// Sample project (the working tree)
// ---------------------------------------------------------------------------
const SAMPLE_FILES = {
    "README.md": "# calc\nA four-function calculator.\n",
    "calc.h": "#pragma once\ndouble eval(const char* expr);\n",
    "main.cpp": "#include \"calc.h\"\nint main() { return 0; }\n",
    "src/lexer.cpp": "// splits input into tokens\n",
    "src/parser.cpp": "// builds the expression tree\n",
    "logs/debug.log": "debug: started\n",
    "build/main.o": "\u007fELF",
};
const HISTORY_LIMIT = 20;  // DEFAULT_HISTORY_LIMIT in CommitHistory.h
const REPO_PATH = "C:\\calc";

// ---------------------------------------------------------------------------
// Helpers (PathUtils.h)
// ---------------------------------------------------------------------------
function reverseString(s) { return s.split("").reverse().join(""); }

function normalizePath(input) {
    let s = String(input).replace(/\\/g, "/");
    if (!s) return "";
    const trailing = s.endsWith("/");
    const absolute = s.startsWith("/");
    const out = [];
    for (const part of s.split("/")) {
        if (part === "" || part === ".") continue;
        if (part === ".." && out.length && out[out.length - 1] !== "..") out.pop();
        else out.push(part);
    }
    let n = (absolute ? "/" : "") + out.join("/");
    if (n === "") return ".";
    if (trailing && n !== "." && !n.endsWith("/")) n += "/";
    return n;
}

function isOutsideRepo(norm) {
    if (!norm) return true;
    if (norm === ".." || norm.startsWith("../")) return true;
    if (norm[0] === "/") return true;
    return norm.length > 1 && norm[1] === ":";
}

function trim(s) { return String(s).replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, ""); }

// SHA-1, as in CommitCommand.cpp (commit ids hash the commit content)
function sha1(text) {
    const bytes = Array.from(new TextEncoder().encode(text));
    const bitLen = bytes.length * 8;
    bytes.push(0x80);
    while (bytes.length % 64 !== 56) bytes.push(0);
    for (let i = 7; i >= 0; i--) bytes.push(i >= 4 ? 0 : (bitLen >>> (i * 8)) & 0xff);
    let h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476, h4 = 0xc3d2e1f0;
    const w = new Array(80);
    for (let c = 0; c < bytes.length; c += 64) {
        for (let i = 0; i < 16; i++) {
            w[i] = (bytes[c + i * 4] << 24) | (bytes[c + i * 4 + 1] << 16) | (bytes[c + i * 4 + 2] << 8) | bytes[c + i * 4 + 3];
        }
        for (let i = 16; i < 80; i++) {
            const v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >>> 31);
        }
        let a = h0, b = h1, cc = h2, d = h3, e = h4;
        for (let i = 0; i < 80; i++) {
            let f, k;
            if (i < 20) { f = (b & cc) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdc; }
            else { f = b ^ cc ^ d; k = 0xca62c1d6; }
            const t = (((a << 5) | (a >>> 27)) + f + e + k + w[i]) | 0;
            e = d; d = cc; cc = (b << 30) | (b >>> 2); b = a; a = t;
        }
        h0 = (h0 + a) | 0; h1 = (h1 + b) | 0; h2 = (h2 + cc) | 0; h3 = (h3 + d) | 0; h4 = (h4 + e) | 0;
    }
    return [h0, h1, h2, h3, h4].map((h) => (h >>> 0).toString(16).padStart(8, "0")).join("");
}

function formatTimestamp(unix) {
    const d = new Date(Number(unix) * 1000);
    const p = (n) => String(n).padStart(2, "0");
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

// ---------------------------------------------------------------------------
// Trie (Trie.cpp): prefix rules match anything that starts with their key
// ---------------------------------------------------------------------------
class Trie {
    constructor() { this.clear(); }
    clear() { this.root = { children: new Map(), terminal: false }; this.count = 0; }
    insert(key, pattern, line, prefixRule) {
        if (!key) return false;
        let cur = this.root;
        for (const ch of key) {
            if (!cur.children.has(ch)) cur.children.set(ch, { children: new Map(), terminal: false });
            cur = cur.children.get(ch);
        }
        if (cur.terminal) return false;
        Object.assign(cur, { terminal: true, prefixRule, line, pattern });
        this.count++;
        return true;
    }
    find(key) {
        let cur = this.root;
        for (const ch of key) {
            cur = cur.children.get(ch);
            if (!cur) return null;
        }
        return cur.terminal ? { pattern: cur.pattern, line: cur.line } : null;
    }
    findMatch(text) {
        let cur = this.root;
        for (let i = 0; ; i++) {
            if (cur.terminal && (cur.prefixRule || i === text.length)) return { pattern: cur.pattern, line: cur.line };
            if (i === text.length) return null;
            cur = cur.children.get(text[i]);
            if (!cur) return null;
        }
    }
}

// ---------------------------------------------------------------------------
// Repository state
// ---------------------------------------------------------------------------
const repo = {};

function resetRepo() {
    repo.files = Object.assign({}, SAMPLE_FILES);   // working tree
    repo.initialized = false;
    repo.index = [];                                 // sorted, like std::set
    repo.objects = new Map();                        // id -> commit
    repo.head = "";
    repo.undo = [];                                  // std::deque: back = last element
    repo.redo = [];
    repo.ignoreLines = [];
    repo.pathTrie = new Trie();
    repo.extTrie = new Trie();
    repo.installed = false;
    repo.historySaved = false;
}

// ---- working tree ----
function isFile(p) { return Object.prototype.hasOwnProperty.call(repo.files, p); }
function isDir(p) {
    if (repo.initialized && /^\.minigit(\/|$)/.test(p)) return true;  // the repository folder itself
    const d = p.endsWith("/") ? p : p + "/";
    return Object.keys(repo.files).some((f) => f.startsWith(d));
}
function syncIgnoreFile() {
    if (repo.initialized || repo.ignoreLines.length) {
        repo.files[".minigitignore"] = repo.ignoreLines.map((l) => l + "\n").join("");
    }
}

// ---- staging index: std::set (binary search keeps it sorted, O(log n)) ----
function indexSearch(path) {
    let lo = 0, hi = repo.index.length;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (repo.index[mid] < path) lo = mid + 1; else hi = mid;
    }
    return lo;
}
function isStaged(p) { const i = indexSearch(p); return repo.index[i] === p; }
function stage(p) {
    const i = indexSearch(p);
    if (repo.index[i] === p) return false;
    repo.index.splice(i, 0, p);
    return true;
}
function unstage(p) {
    const i = indexSearch(p);
    if (repo.index[i] !== p) return false;
    repo.index.splice(i, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Ignore engine (IgnoreManager.cpp)
// ---------------------------------------------------------------------------
function parseRule(raw) {
    const t = trim(raw);
    if (!t || t[0] === "#") return { kind: "NONE" };
    if (t.length > 2 && t[0] === "*" && t[1] === "." && !/[*/\\]/.test(t.slice(2))) {
        return { kind: "EXT", pattern: t, key: reverseString(t.slice(1)) };
    }
    let n = normalizePath(t);
    if (!n || n === "." || isOutsideRepo(n)) return { kind: "NONE" };
    if (!n.endsWith("/") && isDir(n)) n += "/";
    return { kind: n.endsWith("/") ? "DIR" : "FILE", pattern: n, key: n };
}

function rebuildTries() {
    repo.pathTrie.clear();
    repo.extTrie.clear();
    repo.ignoreLines.forEach((line, i) => {
        const r = parseRule(line);
        if (r.kind === "EXT") repo.extTrie.insert(r.key, r.pattern, i + 1, true);
        else if (r.kind === "DIR") repo.pathTrie.insert(r.key, r.pattern, i + 1, true);
        else if (r.kind === "FILE") repo.pathTrie.insert(r.key, r.pattern, i + 1, false);
    });
}

function lookupRule(r) {
    if (r.kind === "EXT") return repo.extTrie.find(r.key);
    if (r.kind === "NONE") return null;
    return repo.pathTrie.find(r.key);
}

function ignoreVerify(path) {
    let n = normalizePath(path);
    if (!n || isOutsideRepo(n)) return { status: "ERROR", message: `invalid path '${path}'` };
    if (!n.endsWith("/") && isDir(n)) n += "/";
    if (n === ".minigit" || n.startsWith(".minigit/")) {
        return { status: "IGNORED", pattern: ".minigit/", line: -1 };
    }
    const a = repo.pathTrie.findMatch(n);
    const b = repo.extTrie.findMatch(reverseString(n));
    if (!a && !b) return { status: "NOT_IGNORED" };
    const best = a && (!b || a.line <= b.line) ? a : b;
    return { status: "IGNORED", pattern: best.pattern, line: best.line };
}
function isIgnored(p) { return ignoreVerify(p).status === "IGNORED"; }

function ignoreAdd(raw) {
    const rule = parseRule(raw);
    if (rule.kind === "NONE") return { status: "ERROR", message: `invalid pattern '${raw}'` };
    const m = lookupRule(rule);
    if (m) return { status: "ALREADY_PRESENT", line: m.line, pattern: m.pattern };
    repo.ignoreLines.push(rule.pattern);
    rebuildTries();
    syncIgnoreFile();
    return { status: "ADDED", line: repo.ignoreLines.length, pattern: rule.pattern };
}

function ignoreRemove(raw) {
    const rule = parseRule(raw);
    if (rule.kind === "NONE") return { status: "ERROR", message: `invalid pattern '${raw}'` };
    let m = lookupRule(rule);
    if (!m && rule.kind === "FILE") m = lookupRule({ kind: "DIR", key: rule.key + "/" });
    if (!m) {
        const res = { status: "NOT_PRESENT" };
        const v = ignoreVerify(raw);
        if (v.status === "IGNORED") {
            res.message = `ignored by '${v.pattern}' (line ${v.line}), remove that rule instead`;
        }
        return res;
    }
    repo.ignoreLines.splice(m.line - 1, 1);
    rebuildTries();
    syncIgnoreFile();
    return { status: "REMOVED", line: m.line, pattern: m.pattern };
}

// ---------------------------------------------------------------------------
// add / add . / add -v / remove (AddCommand.cpp)
// ---------------------------------------------------------------------------
const NOT_A_REPO_ADD = "not a minigit repository (run 'minigit init')";

function stageTree(startRel, r) {
    const prefix = startRel ? startRel.replace(/\/?$/, "/") : "";
    const walk = (dir) => {
        const names = new Set();
        for (const f of Object.keys(repo.files)) {
            if (!f.startsWith(dir)) continue;
            const rest = f.slice(dir.length);
            const slash = rest.indexOf("/");
            names.add(slash === -1 ? rest : rest.slice(0, slash + 1));
        }
        for (const name of [...names].sort()) {
            const rel = dir + name;
            if (name.endsWith("/")) {
                if (isIgnored(rel)) { r.skipped++; continue; }  // skip the whole folder
                walk(rel);
            } else if (isIgnored(rel)) {
                r.skipped++;
            } else if (stage(rel)) {
                r.staged++;
            } else {
                r.already++;
            }
        }
    };
    walk(prefix);
}

function summarize(r) {
    return {
        status: r.staged === 0 && r.already > 0 ? "ALREADY_STAGED" : "STAGED",
        message: `staged ${r.staged} file(s), ${r.already} already staged, skipped ${r.skipped} ignored`,
    };
}

function addAll() {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_ADD };
    const r = { staged: 0, already: 0, skipped: 0 };
    stageTree("", r);
    return summarize(r);
}

function addFile(path) {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_ADD };
    const rel = normalizePath(path);
    if (!rel || isOutsideRepo(rel)) return { status: "ERROR", message: `path '${path}' is outside the repository` };
    if (rel === ".") return addAll();
    const plain = rel.replace(/\/$/, "");
    const dir = isDir(plain);
    if (!dir && !isFile(plain)) return { status: "NOT_FOUND" };
    const ig = ignoreVerify(rel);
    if (ig.status === "IGNORED") return { status: "IGNORED", message: ig.pattern, line: ig.line };
    if (dir) {
        const r = { staged: 0, already: 0, skipped: 0 };
        stageTree(plain, r);
        return summarize(r);
    }
    return stage(plain) ? { status: "STAGED", count: 1 } : { status: "ALREADY_STAGED" };
}

function addVerify(path) {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_ADD };
    const rel = normalizePath(path);
    if (!rel || isOutsideRepo(rel)) return { status: "ERROR", message: `path '${path}' is outside the repository` };
    if (isStaged(rel)) return { status: "IS_STAGED" };
    const ig = ignoreVerify(rel);
    if (ig.status === "IGNORED") return { status: "IGNORED", message: ig.pattern, line: ig.line };
    const plain = rel.replace(/\/$/, "");
    if (!isFile(plain) && !isDir(plain)) return { status: "NOT_FOUND" };
    return { status: "NOT_STAGED" };
}

function removeFile(path) {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_ADD };
    if (path === ".") {
        const count = repo.index.length;
        repo.index = [];
        return count > 0 ? { status: "UNSTAGED", count } : { status: "NOT_STAGED" };
    }
    const rel = normalizePath(path);
    if (!rel || isOutsideRepo(rel)) return { status: "ERROR", message: `path '${path}' is outside the repository` };
    let n = unstage(rel) ? 1 : 0;
    if (n === 0) {  // a folder: unstage every staged file below it
        const prefix = rel.endsWith("/") ? rel : rel + "/";
        for (const f of [...repo.index]) if (f.startsWith(prefix) && unstage(f)) n++;
    }
    return n === 0 ? { status: "NOT_STAGED" } : { status: "UNSTAGED", count: n };
}

// ---------------------------------------------------------------------------
// commit / undo / redo (CommitCommand.cpp, CommitHistory.cpp)
// ---------------------------------------------------------------------------
const NOT_A_REPO_COMMIT = "not a minigit repository (or any of the parent directories)";

function recordOperation(entry) {
    repo.undo.push(entry);
    repo.redo = [];
    while (repo.undo.length > HISTORY_LIMIT) repo.undo.shift();  // pop_front()
    repo.historySaved = true;
}

function commitRepo(message) {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_COMMIT };
    const clean = trim(String(message).replace(/[\r\n]+/g, " "));
    if (!clean) return { status: "ERROR", message: "commit message cannot be empty" };
    if (!repo.index.length) return { status: "ERROR", message: "nothing to commit" };

    const parent = repo.head;
    const timestamp = String(Math.floor(Date.now() / 1000));
    let canonical = `parent ${parent}\ntimestamp ${timestamp}\nmessage ${clean}\n`;
    for (const f of repo.index) {
        const content = repo.files[f] || "";
        canonical += `file ${f} ${content.length}\n${content}\n`;
    }
    const id = sha1(canonical);
    repo.objects.set(id, { id, parent, timestamp, message: clean, files: [...repo.index] });
    recordOperation({ commitId: parent, staged: [...repo.index], message: clean, timestamp });
    repo.head = id;
    const count = repo.index.length;
    repo.index = [];
    return { status: "SUCCESS", commitId: id, message: clean, files: count };
}

function moveHistory(from, to, word) {
    if (!repo.initialized) return { status: "ERROR", message: NOT_A_REPO_COMMIT };
    if (!from.length) return { status: "ERROR", message: `nothing to ${word.toLowerCase()}` };
    to.push({ commitId: repo.head, staged: [...repo.index], message: "", timestamp: "" });
    const restored = from.pop();
    repo.head = restored.commitId;
    repo.index = [...restored.staged].sort();
    return { status: "SUCCESS", commitId: restored.commitId, message: `${word} completed` };
}

// ---------------------------------------------------------------------------
// log (CommitLog.cpp: doubly linked list rebuilt from HEAD)
// ---------------------------------------------------------------------------
function buildCommitList() {
    const newestFirst = [];
    const seen = new Set();
    for (let cur = repo.head; cur && !seen.has(cur); ) {
        seen.add(cur);
        const c = repo.objects.get(cur);
        if (!c) break;
        newestFirst.push(c);
        cur = c.parent;
    }
    // link oldest -> newest, both directions
    const nodes = newestFirst.reverse().map((c) => ({ c, prev: null, next: null }));
    nodes.forEach((n, i) => { n.prev = nodes[i - 1] || null; n.next = nodes[i + 1] || null; });
    return { head: nodes[0] || null, tail: nodes[nodes.length - 1] || null, size: nodes.length };
}

function showLog(reverse) {
    if (!repo.initialized) return { status: "NOT_A_REPO" };
    const list = buildCommitList();
    if (!list.size) return { status: "EMPTY" };
    const entries = [];
    for (let n = reverse ? list.head : list.tail; n; n = reverse ? n.next : n.prev) entries.push(n.c);
    return { status: "OK", entries };
}

// ---------------------------------------------------------------------------
// Output mapping (CLI::print*)
// ---------------------------------------------------------------------------
// The built-in .minigit/ rule has no line in .minigitignore (line -1)
function ruleSource(line) { return line > 0 ? `line ${line}` : "built-in rule"; }

function printAdd(r, p) {
    switch (r.status) {
        case "STAGED": return [0, r.message || `Staged '${p}'`];
        case "ALREADY_STAGED": return [0, r.message || `'${p}' is already staged`];
        case "IGNORED": return [0, `'${p}' is ignored by '${r.message}' (${ruleSource(r.line)})`];
        case "NOT_FOUND": return [1, `'${p}' does not exist`];
        case "UNSTAGED": return [0, r.count > 1 ? `Unstaged ${r.count} files` : `Unstaged '${p}'`];
        case "NOT_STAGED": return [0, `'${p}' is not staged`];
        case "IS_STAGED": return [0, `'${p}' is staged`];
        default: return [1, `error: ${r.message}`];
    }
}

function printIgnore(r, p) {
    switch (r.status) {
        case "ADDED": return [0, `Ignored '${r.pattern}' (line ${r.line})`];
        case "ALREADY_PRESENT": return [0, `'${p}' already ignored at line ${r.line}`];
        case "REMOVED": return [0, `Removed '${r.pattern}' (was line ${r.line})`];
        case "NOT_PRESENT": return [1, `'${p}' is not present in .minigitignore${r.message ? ` (${r.message})` : ""}`];
        case "IGNORED": return [0, `'${p}' is ignored by '${r.pattern}' (${ruleSource(r.line)})`];
        case "NOT_IGNORED": return [0, `'${p}' is not ignored`];
        default: return [1, `error: ${r.message}`];
    }
}

function printCommit(r) {
    if (r.status !== "SUCCESS") return [1, `fatal: ${r.message || "commit failed"}`];
    if (!r.commitId) return [0, r.message];
    let out = `[main ${r.commitId.slice(0, 7)}] ${r.message}`;
    if (r.files > 0) out += `\n${r.files} file(s) committed`;
    return [0, out];
}

function printLog(r) {
    if (r.status === "EMPTY") return [0, "No commits yet"];
    if (r.status === "NOT_A_REPO") return [1, "fatal: not a minigit repository (run 'minigit init')"];
    const out = r.entries.map((e) => {
        let s = `commit ${e.id.slice(0, 7)}\n`;
        if (e.parent) s += `parent  ${e.parent.slice(0, 7)}\n`;
        s += `date    ${formatTimestamp(e.timestamp)}\n\n    ${e.message}\n    (${e.files.length} file(s))`;
        return s;
    });
    return [0, out.join("\n\n")];
}

function validateIgnoreInput(pattern) {
    if (!pattern) return "empty pattern is not allowed";
    if (!trim(pattern)) return "pattern cannot be only whitespace";
    if (isOutsideRepo(normalizePath(pattern))) return `pattern '${pattern}' escapes the repository`;
    return null;
}

// ---------------------------------------------------------------------------
// Command table (CLI.cpp): one row per command; help and usage come from it
// ---------------------------------------------------------------------------
const USAGE = -1;

function isFlag(s, short, long) { return s === short || s === long; }

const COMMANDS = [
    { name: "init", aliases: [], forms: [["init", "Create an empty MiniGit repository or reinitialize an existing one"]],
      run(a) {
          if (a.length) return USAGE;
          const again = repo.initialized;
          repo.initialized = true;
          syncIgnoreFile();
          return [0, `${again ? "Reinitialized existing" : "Initialized empty"} MiniGit repository in ${REPO_PATH}\\.minigit`];
      } },
    { name: "add", aliases: [], forms: [
        ["add <file|dir>", "Stage a file, or every non-ignored file in a directory"],
        ["add .", "Stage every non-ignored file in the repository"],
        ["add -v <file>", "Show whether a file is staged"]],
      run(a) {
          if (a.length === 1) return printAdd(a[0] === "." ? addAll() : addFile(a[0]), a[0]);
          if (a.length === 2 && isFlag(a[0], "-v", "--verify")) return printAdd(addVerify(a[1]), a[1]);
          return USAGE;
      } },
    { name: "remove", aliases: ["rm"], forms: [["remove <file|dir|.>", "Unstage files (files on disk are never touched)"]],
      run(a) { return a.length === 1 ? printAdd(removeFile(a[0]), a[0]) : USAGE; } },
    { name: "ignore", aliases: [], forms: [
        ["ignore", "List the rules in .minigitignore with line numbers"],
        ["ignore <pattern>", "Add a rule to .minigitignore"],
        ["ignore -r <pattern>", "Remove a rule from .minigitignore"],
        ["ignore -v <path>", "Show whether a path is ignored, and by which line"]],
      run(a) {
          if (!a.length) {
              if (!repo.ignoreLines.length) return [0, "No patterns in .minigitignore"];
              return [0, repo.ignoreLines.map((l, i) => `${i + 1}: ${l}`).join("\n")];
          }
          const remove = isFlag(a[0], "-r", "--remove");
          const verify = isFlag(a[0], "-v", "--verify");
          if ((remove || verify) && a.length === 1) return [1, `minigit ignore: option '${a[0]}' requires a pattern argument`];
          if (a.length !== (remove || verify ? 2 : 1)) return USAGE;
          const pattern = a[a.length - 1];
          const err = validateIgnoreInput(pattern);
          if (err) return [1, `minigit ignore: ${err}`];
          const r = remove ? ignoreRemove(pattern) : verify ? ignoreVerify(pattern) : ignoreAdd(pattern);
          return printIgnore(r, pattern);
      } },
    { name: "commit", aliases: [], forms: [
        ['commit "<message>"', "Record the staged files as a new commit"],
        ['commit -m "<message>"', "Same, with an explicit -m flag"],
        ["commit undo", "Undo the last commit and restore its staged files"],
        ["commit redo", "Redo the last undone commit"]],
      run(a) {
          if (a.length === 1 && a[0] === "undo") return printCommit(moveHistory(repo.undo, repo.redo, "Undo"));
          if (a.length === 1 && a[0] === "redo") return printCommit(moveHistory(repo.redo, repo.undo, "Redo"));
          if (a.length && isFlag(a[0], "-m", "--message")) {
              if (a.length === 1) return [1, `fatal: option '${a[0]}' requires a message argument`];
              return a.length === 2 ? printCommit(commitRepo(a[1])) : USAGE;
          }
          return a.length === 1 ? printCommit(commitRepo(a[0])) : USAGE;
      } },
    { name: "undo", aliases: [], forms: [["undo", "Shortcut for 'commit undo'"]],
      run(a) { return a.length ? USAGE : printCommit(moveHistory(repo.undo, repo.redo, "Undo")); } },
    { name: "redo", aliases: [], forms: [["redo", "Shortcut for 'commit redo'"]],
      run(a) { return a.length ? USAGE : printCommit(moveHistory(repo.redo, repo.undo, "Redo")); } },
    { name: "log", aliases: [], forms: [["log [--reverse]", "Show commit history, newest first (oldest first with --reverse)"]],
      run(a) {
          if (!a.length) return printLog(showLog(false));
          if (a.length === 1 && a[0] === "--reverse") return printLog(showLog(true));
          return USAGE;
      } },
    { name: "install", aliases: ["--install"], forms: [["install", "Copy MiniGit into a folder on your PATH"]],
      run(a) {
          if (a.length) return USAGE;
          repo.installed = true;
          return [0, "Successfully installed MiniGit!\n  Installed binary to: C:\\Users\\you\\AppData\\Local\\Microsoft\\WindowsApps\\minigit.exe\n\nYou can now run 'minigit' directly from any folder in PowerShell or Command Prompt!\n(sandbox: nothing was written to your computer)"];
      } },
    { name: "uninstall", aliases: ["--uninstall"], forms: [["uninstall", "Remove the installed copy from your PATH"]],
      run(a) {
          if (a.length) return USAGE;
          if (!repo.installed) return [0, "MiniGit is not currently installed in WindowsApps"];
          repo.installed = false;
          return [0, "Successfully uninstalled MiniGit from:\n  C:\\Users\\you\\AppData\\Local\\Microsoft\\WindowsApps\\minigit.exe"];
      } },
    { name: "help", aliases: ["--help", "-h"], forms: [["help [<command>]", "Show this list, or the usage of one command"]],
      run(a) {
          if (!a.length) return [0, usageText()];
          if (a.length !== 1) return USAGE;
          const c = findCommand(a[0]);
          if (!c) return [1, `minigit help: '${a[0]}' is not a minigit command`];
          const width = Math.max(...c.forms.map((f) => f[0].length));
          return [0, `${commandUsage(c)}\n\n${c.forms.map((f) => `   ${f[0].padEnd(width + 2)}${f[1]}`).join("\n")}`];
      } },
];

function findCommand(name) {
    return COMMANDS.find((c) => c.name === name || c.aliases.includes(name)) || null;
}
function commandUsage(c) { return "usage: " + c.forms.map((f) => `minigit ${f[0]}`).join(" | "); }
function usageText() {
    const width = Math.max(...COMMANDS.flatMap((c) => c.forms.map((f) => f[0].length)));
    const rows = COMMANDS.flatMap((c) => c.forms.map((f) => `   ${f[0].padEnd(width + 2)}${f[1]}`));
    return `usage: minigit <command> [<args>]\n\nThese are common MiniGit commands:\n${rows.join("\n")}\n\nSee 'minigit help <command>' for the usage of one command.`;
}

// Levenshtein distance, two rolling rows (as in CLI.cpp)
function editDistance(a, b) {
    let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
    for (let i = 1; i <= a.length; i++) {
        const cur = [i];
        for (let j = 1; j <= b.length; j++) {
            cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
        }
        prev = cur;
    }
    return prev[b.length];
}
function suggestCommand(typo) {
    let best = "", bestD = 3;
    for (const c of COMMANDS) {
        const d = editDistance(typo, c.name);
        if (d < bestD) { bestD = d; best = c.name; }
    }
    return best;
}

function dispatch(args) {
    if (!args.length) return [1, usageText()];
    const [name, ...rest] = args;
    const c = findCommand(name);
    if (!c) {
        const guess = suggestCommand(name);
        return [1, `minigit: '${name}' is not a minigit command. See 'minigit --help'.` +
            (guess ? `\n\nThe most similar command is\n    ${guess}` : "")];
    }
    if (rest.length === 1 && (rest[0] === "--help" || rest[0] === "-h")) return [0, commandUsage(c)];
    const res = c.run(rest);
    return res === USAGE ? [1, commandUsage(c)] : res;
}

// Shell-style tokenizer: "quoted words" stay together
function tokenize(line) {
    const out = [];
    const re = /"([^"]*)"|'([^']*)'|(\S+)/g;
    let m;
    while ((m = re.exec(line))) out.push(m[1] ?? m[2] ?? m[3]);
    return out;
}

// Which data-structure view a command should bring forward
function viewFor(name) {
    if (name === "add" || name === "remove" || name === "rm") return "index";
    if (name === "ignore") return "trie";
    if (name === "undo" || name === "redo") return "deque";
    if (name === "commit" || name === "log") return "commits";
    if (name === "init") return "fs";
    return null;
}

// ---------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------
const $ = (sel) => document.querySelector(sel);
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

function escapeHtml(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function printLine(text, kind) {
    const out = $("#terminalOutput");
    const line = document.createElement("div");
    line.className = `term-line ${kind}`;
    line.textContent = text;
    out.appendChild(line);
    const body = $("#terminalBody");
    body.scrollTop = body.scrollHeight;
}

function runCommand(raw) {
    const line = trim(raw);
    if (!line) return;
    printLine(line, "cmd");
    let args = tokenize(line);
    const first = args[0];

    if (first === "clear" || first === "cls") { $("#terminalOutput").textContent = ""; return; }
    if (first === "ls" || first === "dir") {
        printLine(Object.keys(repo.files).sort().join("\n"), "out");
        return;
    }
    if (first === "minigit" || first === "minigit.exe" || first === "./minigit" || first === ".\\minigit") {
        args = args.slice(1);
    } else if (!findCommand(first)) {
        printLine(`${first}: not recognised here. Commands start with minigit, for example: minigit help`, "err");
        return;
    }

    const [code, text] = dispatch(args);
    if (text) printLine(text, code === 0 ? "out" : "err");
    renderViews();
    const view = viewFor(args[0]);
    if (view) selectTab(view, false);
}

// ---------------------------------------------------------------------------
// Data structure views
// ---------------------------------------------------------------------------
function emptyState(html) { return `<div class="empty">${html}</div>`; }

function renderCommits() {
    const el = $("#viz-commits");
    const list = buildCommitList();
    if (!repo.initialized || !list.size) {
        el.innerHTML = emptyState(`No commits on <code>main</code> yet. Stage files, then <code>minigit commit "message"</code>.`);
        return;
    }
    const parts = [];
    for (let n = list.head; n; n = n.next) {
        const isHead = n.c.id === repo.head;
        parts.push(`<div class="node${isHead ? " head" : ""}"><code>${n.c.id.slice(0, 7)}${isHead ? "  HEAD" : ""}</code><p>${escapeHtml(n.c.message)}</p><small>${n.c.files.length} file(s)</small></div>`);
        if (n.next) parts.push(`<span class="link" aria-hidden="true">&lt;-&gt;</span>`);
    }
    const unreachable = repo.objects.size - list.size;
    el.innerHTML = `<div class="viz-meta"><span><strong>Doubly linked list</strong>, oldest to newest, rebuilt from HEAD</span><span>${list.size} reachable${unreachable ? `, ${unreachable} undone (still in objects/)` : ""}</span></div><div class="dll">${parts.join("")}</div>`;
}

function renderIndex() {
    const el = $("#viz-index");
    if (!repo.initialized) { el.innerHTML = emptyState(`Run <code>minigit init</code> to create <code>.minigit/index</code>.`); return; }
    if (!repo.index.length) { el.innerHTML = emptyState(`The index is empty. Try <code>minigit add .</code>`); return; }
    const items = repo.index.map((p, i) => `<li>${escapeHtml(p)}<span>${i}</span></li>`).join("");
    el.innerHTML = `<div class="viz-meta"><span><strong>std::set</strong>, kept sorted, no duplicates</span><span>${repo.index.length} path(s)</span></div><ul class="set-list">${items}</ul>`;
}

// Draw a trie with single-child chains collapsed into one edge label
function trieLines(node, prefix, out, display) {
    const kids = [...node.children.entries()].sort((a, b) => a[0].localeCompare(b[0]));
    kids.forEach(([ch, child], i) => {
        let label = ch;
        let cur = child;
        while (!cur.terminal && cur.children.size === 1) {
            const [c2, n2] = cur.children.entries().next().value;
            label += c2;
            cur = n2;
        }
        const last = i === kids.length - 1;
        const tag = cur.terminal ? `   <b>line ${cur.line}: ${escapeHtml(cur.pattern)}</b>` : "";
        out.push(`${prefix}${last ? "`-- " : "|-- "}${escapeHtml(display(label))}${tag}`);
        trieLines(cur, prefix + (last ? "    " : "|   "), out, display);
    });
}

function renderTrie() {
    const el = $("#viz-trie");
    if (!repo.ignoreLines.length) {
        el.innerHTML = emptyState(`No rules yet. Try <code>minigit ignore *.log</code> or <code>minigit ignore build/</code>.`);
        return;
    }
    const draw = (trie) => {
        if (!trie.count) return "(empty)";
        const out = ["(root)"];
        trieLines(trie.root, "", out, (s) => s);
        return out.join("\n");
    };
    const rules = repo.ignoreLines.map((l, i) => {
        const r = parseRule(l);
        const kind = r.kind === "EXT" ? `extension, key ${escapeHtml(r.key)}` : r.kind === "DIR" ? "folder prefix" : "exact file";
        return `<li><span>${i + 1}</span>${escapeHtml(l)}  <span>(${kind})</span></li>`;
    }).join("");
    el.innerHTML = `<div class="viz-meta"><span><strong>Two tries</strong>: forward for paths, reversed for extensions</span><span>${repo.ignoreLines.length} rule(s)</span></div>
        <div class="tries"><div><h4>Path trie <span>(files, folders)</span></h4><pre class="trie-tree">${draw(repo.pathTrie)}</pre></div>
        <div><h4>Extension trie <span>(keys stored reversed)</span></h4><pre class="trie-tree">${draw(repo.extTrie)}</pre></div></div>
        <ul class="rules" aria-label=".minigitignore">${rules}</ul>`;
}

function laneHtml(items, label) {
    if (!items.length) return `<div class="lane" aria-label="${label}, empty"><span class="lane-item"><small>empty</small></span></div>`;
    return `<div class="lane" aria-label="${label}">${items.map((s, i) => {
        const id = s.commitId ? s.commitId.slice(0, 7) : "no commit";
        return `<span class="lane-item${i === items.length - 1 ? " back" : ""}">HEAD ${id}<small>${s.staged.length} staged</small></span>`;
    }).join("")}</div><div class="lane-ends"><span>front</span><span>back</span></div>`;
}

function renderDeques() {
    const el = $("#viz-deque");
    if (!repo.initialized) { el.innerHTML = emptyState(`Run <code>minigit init</code>, then commit to start recording states.`); return; }
    el.innerHTML = `<div class="viz-meta"><span><strong>Two std::deque</strong>; a commit pushes onto undo and clears redo</span><span>limit ${HISTORY_LIMIT}</span></div>
        <div class="deques"><div><h4>Undo deque <span>(${repo.undo.length})</span></h4>${laneHtml(repo.undo, "Undo deque")}</div>
        <div><h4>Redo deque <span>(${repo.redo.length})</span></h4>${laneHtml(repo.redo, "Redo deque")}</div></div>`;
}

function renderFs() {
    const el = $("#viz-fs");
    const rows = [];
    const pad = (s, n) => s + " ".repeat(Math.max(1, n - s.length));
    if (repo.initialized) {
        rows.push("<b>.minigit/</b>");
        rows.push(`  ${pad("HEAD", 20)}<s>ref: refs/heads/main</s>`);
        if (repo.head || repo.historySaved) rows.push(`  ${pad("refs/heads/main", 20)}<i>${repo.head ? repo.head.slice(0, 7) : "(empty)"}</i>`);
        else rows.push(`  ${pad("refs/heads/", 20)}<s>(no commits yet)</s>`);
        rows.push(`  ${pad("index", 20)}<s>${repo.index.length} path(s)</s>`);
        if (repo.historySaved) rows.push(`  ${pad("history", 20)}<s>${repo.undo.length} undo, ${repo.redo.length} redo</s>`);
        rows.push(`  ${pad("objects/", 20)}<s>${repo.objects.size} commit(s)</s>`);
        for (const c of repo.objects.values()) rows.push(`    ${pad(c.id.slice(0, 7) + "/", 18)}<s>metadata, snapshot/ (${c.files.length})</s>`);
    } else {
        rows.push("<s>(no .minigit/ yet: run minigit init)</s>");
    }
    rows.push("");
    for (const f of Object.keys(repo.files).sort()) {
        let status = "";
        if (repo.initialized && isStaged(f)) status = "<i>staged</i>";
        else {
            const v = ignoreVerify(f);
            if (v.status === "IGNORED") status = `<s>ignored by ${escapeHtml(v.pattern)}</s>`;
        }
        rows.push(`${pad(escapeHtml(f), 22)}${status}`);
    }
    el.innerHTML = `<div class="viz-meta"><span><strong>~/calc</strong>, the sample project on disk</span><span>${repo.initialized ? "repository" : "not a repository"}</span></div><pre class="fs">${rows.join("\n")}</pre>`;
}

function renderViews() {
    renderCommits();
    renderIndex();
    renderTrie();
    renderDeques();
    renderFs();
}

// ---------------------------------------------------------------------------
// Tabs (ARIA tabs with arrow-key navigation)
// ---------------------------------------------------------------------------
function selectTab(name, focus) {
    document.querySelectorAll(".viz-tab").forEach((t) => {
        const on = t.id === `tab-${name}`;
        t.setAttribute("aria-selected", String(on));
        t.tabIndex = on ? 0 : -1;
        if (on && focus) t.focus();
    });
    document.querySelectorAll(".viz-view").forEach((v) => v.classList.toggle("active", v.id === `viz-${name}`));
}

function setupTabs() {
    const tabs = [...document.querySelectorAll(".viz-tab")];
    tabs.forEach((tab, i) => {
        tab.addEventListener("click", () => selectTab(tab.id.slice(4), false));
        tab.addEventListener("keydown", (e) => {
            const step = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
            if (!step) return;
            e.preventDefault();
            selectTab(tabs[(i + step + tabs.length) % tabs.length].id.slice(4), true);
        });
    });
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
const PRESETS = {
    track: ["minigit init", "minigit add main.cpp", "minigit add -v main.cpp", "minigit add .", 'minigit commit -m "first commit"', "minigit log"],
    ignore: ["minigit init", "minigit ignore *.log", "minigit ignore build/", "minigit ignore -v logs/debug.log", "minigit ignore -v build/main.o", "minigit add .", "minigit ignore"],
    undo: ["minigit init", "minigit add .", 'minigit commit -m "first commit"', "minigit add .", 'minigit commit -m "second commit"', "minigit undo", "minigit redo", "minigit undo", "minigit log"],
};
let presetTimer = null;

function resetSandbox(announce) {
    clearTimeout(presetTimer);
    resetRepo();
    $("#terminalOutput").textContent = "";
    if (announce) printLine("Sandbox reset: back to the sample project, no .minigit/.", "note");
    renderViews();
    selectTab("commits", false);
}

function playPreset(name) {
    resetSandbox(false);
    const steps = PRESETS[name];
    const buttons = document.querySelectorAll("[data-preset]");
    buttons.forEach((b) => (b.disabled = true));
    let i = 0;
    const next = () => {
        runCommand(steps[i++]);
        if (i < steps.length) presetTimer = setTimeout(next, reduceMotion.matches ? 0 : 450);
        else buttons.forEach((b) => (b.disabled = false));
    };
    next();
}

// ---------------------------------------------------------------------------
// Page chrome: theme, menu, active nav, reveal, PDF viewer
// ---------------------------------------------------------------------------
function setupTheme() {
    const btn = $("#themeToggle");
    const apply = (t) => {
        document.documentElement.setAttribute("data-theme", t);
        const light = t === "light";
        btn.innerHTML = `<i class="ph ${light ? "ph-moon" : "ph-sun"}" aria-hidden="true"></i>`;
        btn.setAttribute("aria-label", light ? "Switch to dark theme" : "Switch to light theme");
    };
    apply(document.documentElement.getAttribute("data-theme") || "dark");
    btn.addEventListener("click", () => {
        const t = document.documentElement.getAttribute("data-theme") === "light" ? "dark" : "light";
        apply(t);
        try { localStorage.setItem("minigit-theme", t); } catch (e) { /* private mode */ }
    });
}

function setupMenu() {
    const btn = $("#mobileMenuToggle");
    const nav = $("#primaryNav");
    const set = (open) => {
        nav.classList.toggle("open", open);
        btn.setAttribute("aria-expanded", String(open));
        btn.setAttribute("aria-label", open ? "Close menu" : "Open menu");
        btn.innerHTML = `<i class="ph ${open ? "ph-x" : "ph-list"}" aria-hidden="true"></i>`;
    };
    btn.addEventListener("click", () => set(!nav.classList.contains("open")));
    nav.addEventListener("click", (e) => { if (e.target.closest("a")) set(false); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape") set(false); });
}

function setupObservers() {
    if (!("IntersectionObserver" in window)) {
        document.querySelectorAll(".reveal").forEach((el) => el.classList.add("in"));
        return;
    }
    // header border once the hero top leaves the viewport
    const header = $("#siteHeader");
    new IntersectionObserver(([e]) => header.classList.toggle("scrolled", !e.isIntersecting))
        .observe($("#overview"));

    // active nav link for the section in view
    const links = [...document.querySelectorAll(".nav-link[data-section]")];
    const spy = new IntersectionObserver((entries) => {
        entries.forEach((e) => {
            if (!e.isIntersecting) return;
            links.forEach((l) => {
                const on = l.dataset.section === e.target.id;
                l.classList.toggle("active", on);
                if (on) l.setAttribute("aria-current", "true"); else l.removeAttribute("aria-current");
            });
        });
    }, { rootMargin: "-30% 0px -60% 0px" });
    links.forEach((l) => { const s = document.getElementById(l.dataset.section); if (s) spy.observe(s); });

    // reveal once
    const rev = new IntersectionObserver((entries) => {
        entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); rev.unobserve(e.target); } });
    }, { rootMargin: "0px 0px -8% 0px" });
    document.querySelectorAll(".reveal").forEach((el) => rev.observe(el));
}

function setupPdf() {
    // One entry per phase and document; file: null means not published yet.
    const docs = {
        1: {
            report: { file: "Reports/report.pdf", title: "MiniGit Phase 1 design report", desc: "Problem, scope, architecture, data structure choices and the three-phase plan.", name: "MiniGit-Phase1-Report.pdf" },
            slides: { file: "Reports/ppt.pdf", title: "MiniGit Phase 1 presentation", desc: "The slide deck: motivation, layered design, command set and roadmap.", name: "MiniGit-Phase1-Presentation.pdf" },
        },
        2: {
            report: { file: "Reports/phase2.pdf", title: "MiniGit Phase 2 progress report", desc: "What was built: the four modules, the updated architecture, who did what, roadblocks and the remaining work.", name: "MiniGit-Phase2-Report.pdf" },
            slides: { file: null, title: "MiniGit Phase 2 presentation", desc: "The slide deck for the Phase 2 review." },
        },
    };
    const state = { phase: "2", kind: "report" };
    const phaseBtns = document.querySelectorAll(".pdf-tab[data-phase]");
    const kindBtns = document.querySelectorAll(".pdf-tab[data-kind]");
    const frame = $("#pdfFrame");

    function show() {
        const d = docs[state.phase][state.kind];
        phaseBtns.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.phase === state.phase)));
        kindBtns.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.kind === state.kind)));
        $("#pdfDocTitle").textContent = d.title;
        $("#pdfDocDesc").textContent = d.desc;
        const ready = Boolean(d.file);
        frame.hidden = !ready;
        $("#pdfEmpty").hidden = ready;
        $("#pdfActions").hidden = !ready;
        if (!ready) return;
        const src = `${d.file}#view=FitH`;
        if (frame.getAttribute("src") !== src) frame.src = src;
        $("#pdfOpenTabBtn").href = d.file;
        const dl = $("#pdfDownloadBtn");
        dl.href = d.file;
        dl.download = d.name;
    }

    phaseBtns.forEach((b) => b.addEventListener("click", () => { state.phase = b.dataset.phase; show(); }));
    kindBtns.forEach((b) => b.addEventListener("click", () => { state.kind = b.dataset.kind; show(); }));
    $("#pdfEmptyBack").addEventListener("click", () => { state.kind = "report"; show(); });
    const box = $("#pdfViewerContainer");
    $("#pdfFullscreenBtn").addEventListener("click", () => {
        if (document.fullscreenElement) document.exitFullscreen();
        else if (box.requestFullscreen) box.requestFullscreen().catch(() => {});
    });
}

// ---------------------------------------------------------------------------
// Interactive architecture diagram
// Every node and edge lists the traces it belongs to ("core" = always part of a trace).
// ---------------------------------------------------------------------------
const ARCH_NODES = [
    // layer, id, x, y, w, title, sub, mono flags [title, sub], traces, planned
    ["cli", "input", 115, 68, 170, "minigit <cmd>", "terminal input", [1, 0], "core"],
    ["cli", "disp", 510, 68, 380, "Dispatcher and command table", "find command, check arguments, help and typo hints", [0, 0], "core"],
    ["cli", "out", 1190, 68, 220, "Output", "message and exit code", [0, 0], "core"],
    ["mod", "m-init", 115, 238, 170, "init", "InitCommand", [1, 0], "init"],
    ["mod", "m-add", 315, 238, 170, "add, remove", "AddCommand", [1, 0], "add"],
    ["mod", "m-ignore", 515, 238, 170, "ignore", "IgnoreManager", [1, 0], "ignore"],
    ["mod", "m-commit", 715, 238, 170, "commit, undo, redo", "CommitCommand", [1, 0], "commit undo"],
    ["mod", "m-log", 915, 238, 170, "log", "LogCommand", [1, 0], "log"],
    ["mod", "m-checkout", 1115, 238, 170, "checkout, rollback", "PHASE 3", [1, 0], "checkout", true],
    ["mod", "m-branch", 1315, 238, 170, "branch, switch", "FUTURE", [1, 0], "branch", true],
    ["ds", "d-set", 315, 398, 170, "Ordered set", "std::set", [0, 1], "add"],
    ["ds", "d-trie", 515, 398, 170, "Dual trie", "paths, reversed extensions", [0, 0], "add ignore"],
    ["ds", "d-deque", 715, 398, 170, "Two deques", "undo and redo states", [0, 0], "commit undo"],
    ["ds", "d-list", 915, 398, 170, "Doubly linked list", "CommitLog", [0, 1], "log"],
    ["ds", "d-stack", 1115, 398, 170, "Stack", "previous versions", [0, 0], "checkout", true],
    ["ds", "d-graph", 1315, 398, 170, "Commit graph", "DAG of branches", [0, 0], "branch", true],
    ["st", "s-head", 115, 568, 170, "HEAD", "refs/heads/main", [1, 1], "init commit undo log"],
    ["st", "s-index", 315, 568, 170, "index", "staged paths", [1, 0], "init add commit"],
    ["st", "s-ignore", 515, 568, 170, ".minigitignore", "one rule per line", [1, 0], "init add ignore"],
    ["st", "s-history", 715, 568, 170, "history", "saved undo/redo states", [1, 0], "commit undo"],
    ["st", "s-objects", 915, 568, 170, "objects/<id>/", "metadata, snapshot", [1, 0], "commit log checkout"],
    ["st", "s-work", 1115, 568, 170, "Working files", "restored from snapshot", [0, 0], "checkout", true],
    ["st", "s-refs", 1315, 568, 170, "refs/heads/*", "one ref per branch", [1, 0], "branch", true],
];

const ARCH_EDGES = [
    // kind, path, traces, options: both = arrow at both ends, end = no arrow, label [x, y, text, anchor]
    ["call", "M285 99 H507", "core"],
    ["call", "M890 99 H1187", "core", { label: [1040, 88, "Result struct from the module", "middle"] }],
    ["call", "M700 130 V178", "core", { end: false, label: [712, 168, "calls one module"] }],
    ["call", "M200 178 H400", "init", { end: false }],
    ["call", "M400 178 H600", "init add", { end: false }],
    ["call", "M600 178 H700", "init add ignore", { end: false }],
    ["call", "M700 178 H800", "commit undo log checkout branch", { end: false }],
    ["call", "M800 178 H1000", "log checkout branch", { end: false }],
    ["plan", "M1000 178 H1200", "checkout branch", { end: false }],
    ["plan", "M1200 178 H1400", "branch", { end: false }],
    ["call", "M200 178 V235", "init"],
    ["call", "M400 178 V235", "add"],
    ["call", "M600 178 V235", "ignore"],
    ["call", "M800 178 V235", "commit undo"],
    ["call", "M1000 178 V235", "log"],
    ["plan", "M1200 178 V235", "checkout"],
    ["plan", "M1400 178 V235", "branch"],
    ["look", "M485 260 H505 V340 H560 V395", "add", { label: [420, 336, "is it ignored?"] }],
    ["call", "M400 300 V395", "add"],
    ["call", "M600 300 V395", "ignore"],
    ["call", "M800 300 V395", "commit undo"],
    ["call", "M1000 300 V395", "log"],
    ["plan", "M1200 300 V395", "checkout"],
    ["plan", "M1400 300 V395", "branch"],
    ["file", "M200 300 V565", "init", { label: [210, 438, "creates .minigit/"] }],
    ["file", "M400 463 V565", "add", { both: true, label: [408, 503, "load / save"] }],
    ["file", "M600 463 V565", "add ignore", { both: true, label: [608, 503, "load / save"] }],
    ["file", "M800 463 V565", "commit undo", { both: true, label: [808, 503, "load / save"] }],
    ["file", "M885 280 H900 V599 H912", "commit", { label: [906, 342, "writes"] }],
    ["file", "M1000 566 V463", "log", { label: [1008, 503, "reads commits"] }],
    // trace-only: hidden in the overview, drawn under the storage row when their command is traced
    ["file", "M115 270 H95 V648 H600 V633", "init", { only: true, label: [232, 669, "creates index and .minigitignore"] }],
    ["file", "M400 648 V633", "init", { only: true }],
    ["file", "M715 290 H700 V648 H200 V633", "commit", { only: true, label: [208, 669, "updates the ref"] }],
    ["file", "M400 648 V633", "commit", { only: true, label: [408, 669, "reads, then clears"] }],
    ["file", "M715 290 H700 V648 H200 V633", "undo", { only: true, label: [208, 669, "moves the ref"] }],
    ["file", "M200 633 V648 H1092 V280 H1088", "log", { only: true, label: [208, 669, "reads HEAD first"] }],
    ["plan", "M1200 463 V565", "checkout"],
    ["plan", "M1400 463 V565", "branch"],
    ["plan", "M1085 599 H1112", "checkout"],
];

const ARCH_TRACES = {
    init: {
        kicker: "Command", title: "minigit init", col: 200,
        hot: ["m-init", "s-head"],
        lead: "Creates the repository. No data structure is involved; it only lays out folders and files.",
        steps: [
            [["disp"], "The dispatcher finds <code>init</code> in the command table and rejects any extra arguments."],
            [["m-init"], "<code>InitCommand</code> creates <code>.minigit/</code> with <code>objects/</code> and <code>refs/heads/</code>."],
            [["s-head"], "<code>HEAD</code> is written as <code>ref: refs/heads/main</code>."],
            [["s-index", "s-ignore"], "An empty <code>index</code> and <code>.minigitignore</code> are created."],
            [["out"], "Run it again and it prints <em>Reinitialized</em>; existing files are never overwritten."],
        ],
        facts: [["Source", "InitCommand.cpp"], ["Data structure", "None, filesystem only"], ["Cost", "O(1)"], ["Writes", ".minigit/HEAD, index, .minigitignore"]],
    },
    add: {
        kicker: "Command", title: "minigit add <file|.>", col: 400,
        hot: ["m-add", "d-set", "s-index"],
        lead: "Stages files. remove takes a file, a folder or . back out of the same set.",
        steps: [
            [["m-add"], "<code>AddCommand</code> normalises the path, so backslashes and <code>./</code> prefixes all match one form."],
            [["d-trie", "s-ignore"], "It asks the ignore engine whether the path is ignored. If so, it reports the matching rule and its line."],
            [["d-set"], "Otherwise the path goes into the <code>std::set</code>. A path that is already staged is not added twice."],
            [["s-index"], "The set is saved to <code>.minigit/index</code>, one path per line, already in sorted order."],
        ],
        facts: [["Source", "AddCommand.cpp, StagingArea.cpp"], ["Data structure", "Ordered set (red-black tree)"], ["Cost", "O(log n) per file"], ["Writes", ".minigit/index"]],
    },
    ignore: {
        kicker: "Command", title: "minigit ignore <pattern>", col: 600,
        hot: ["m-ignore", "d-trie", "s-ignore"],
        lead: "Adds, checks (-v) and removes (-r) ignore rules.",
        steps: [
            [["m-ignore"], "The pattern is checked first: no absolute paths, <code>..</code> or drive letters."],
            [["s-ignore"], "<code>IgnoreManager</code> loads <code>.minigitignore</code> and numbers each rule by its line."],
            [["d-trie"], "Files and folders go into the path trie. <code>*.ext</code> rules go into a second trie with the extension reversed, so a suffix match becomes a prefix walk."],
            [["out"], "<code>ignore -v &lt;path&gt;</code> names the rule and line that matched; <code>ignore -r</code> removes a rule and rebuilds the tries."],
        ],
        facts: [["Source", "IgnoreManager.cpp, Trie.cpp"], ["Data structure", "Dual trie"], ["Cost", "O(L), L = path length"], ["Writes", ".minigitignore"]],
    },
    commit: {
        kicker: "Command", title: "minigit commit \"<message>\"", col: 800,
        hot: ["m-commit", "d-deque", "s-history", "s-objects"],
        lead: "Saves a version of every staged file.",
        steps: [
            [["s-index"], "Reads the staged paths from <code>index</code>. With nothing staged, it stops: <em>nothing to commit</em>."],
            [["s-objects"], "Hashes parent, time, message and file contents into a SHA-1 id, then writes <code>objects/&lt;id&gt;/metadata</code> and a snapshot of each file."],
            [["d-deque"], "The previous state is pushed onto the undo deque and the redo deque is cleared. Past 20 entries, the oldest is dropped from the front."],
            [["s-history"], "Both deques are saved to <code>.minigit/history</code>."],
            [["s-head"], "<code>refs/heads/main</code> now points at the new commit, and the index is cleared."],
        ],
        facts: [["Source", "CommitCommand.cpp, CommitHistory.cpp"], ["Data structure", "Two deques, limit 20"], ["Cost", "O(1) push and pop at both ends"], ["Writes", "objects/<id>/, history, refs/heads/main"]],
    },
    undo: {
        kicker: "Command", title: "minigit undo / redo", col: 800,
        hot: ["m-commit", "d-deque", "s-history"],
        lead: "Moves between saved states. Commit objects are never deleted.",
        steps: [
            [["s-history", "d-deque"], "The deques are loaded from <code>.minigit/history</code>. An empty undo deque prints <em>nothing to undo</em>."],
            [["d-deque"], "undo pops the newest state from the undo deque and pushes the current one onto redo. redo does the reverse."],
            [["s-head"], "The branch ref moves to the restored commit id."],
            [["s-history"], "Both deques are saved again, so undo still works in the next session."],
        ],
        facts: [["Source", "CommitCommand.cpp, CommitHistory.cpp"], ["Data structure", "Two deques"], ["Cost", "O(1)"], ["Writes", "history, refs/heads/main"]],
    },
    log: {
        kicker: "Command", title: "minigit log [--reverse]", col: 1000,
        hot: ["m-log", "d-list", "s-objects"],
        lead: "Prints the history with id, message and time.",
        steps: [
            [["s-head"], "Reads the commit id that <code>HEAD</code> points to."],
            [["s-objects"], "Reads that commit's metadata and follows parent ids back to the first commit."],
            [["d-list"], "Each commit becomes a node in <code>CommitLog</code>, a doubly linked list."],
            [["out"], "<code>log</code> walks newest to oldest with <code>prev()</code>; <code>--reverse</code> walks oldest to newest with <code>next()</code>, with no copy or re-sort."],
        ],
        facts: [["Source", "LogCommand.cpp, CommitLog.cpp"], ["Data structure", "Doubly linked list"], ["Cost", "O(n) walk in either direction"], ["Writes", "Nothing, read only"]],
    },
    checkout: {
        kicker: "Planned, Phase 3", title: "minigit checkout / rollback", col: 1200,
        hot: ["m-checkout", "d-stack", "s-work"], planned: true,
        lead: "Not built yet. This is the plan from the Phase 1 design report.",
        steps: [
            [["m-checkout"], "<code>checkout &lt;id&gt;</code> will restore the files of a chosen commit."],
            [["d-stack"], "Before restoring, the current version is pushed onto a stack."],
            [["s-objects", "s-work"], "Files are copied back from <code>objects/&lt;id&gt;/snapshot</code> into the working folder."],
            [["d-stack"], "<code>rollback</code> pops the stack to undo the last checkout."],
        ],
        facts: [["Status", "Phase 3"], ["Data structure", "Stack (LIFO)"], ["Cost", "O(1) push and pop"], ["Writes", "Working files"]],
    },
    branch: {
        kicker: "Future idea", title: "minigit branch / switch", col: 1400,
        hot: ["m-branch", "d-graph", "s-refs"], planned: true,
        lead: "Out of scope for this project: MiniGit keeps one linear history on main. Shown to explain why refs/heads/ already exists.",
        steps: [
            [["m-branch"], "<code>branch &lt;name&gt;</code> would create a new ref; <code>switch</code> would point HEAD at it."],
            [["d-graph"], "History would become a graph: two commits can share a parent."],
            [["s-refs"], "Each branch is one file under <code>refs/heads/</code>, like <code>main</code> today."],
        ],
        facts: [["Status", "Not planned"], ["Data structure", "Directed acyclic graph"], ["Cost", "O(V + E) to walk"], ["Writes", "refs/heads/<name>"]],
    },
};

function setupArch() {
    const svg = $("#archSvg");
    if (!svg) return;
    const NS = "http://www.w3.org/2000/svg";
    const el = (tag, attrs, parent) => {
        const e = document.createElementNS(NS, tag);
        for (const k in attrs) e.setAttribute(k, attrs[k]);
        if (parent) parent.appendChild(e);
        return e;
    };
    const text = (parent, cls, x, y, s, anchor) => {
        const t = el("text", { class: cls, x, y }, parent);
        if (anchor) t.setAttribute("text-anchor", anchor);
        t.textContent = s;
        return t;
    };

    const defs = el("defs", {}, svg);
    for (const k of ["call", "file", "look", "plan"]) {
        const m = el("marker", { id: `ar-${k}`, viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" }, defs);
        el("path", { d: "M0,0 L10,5 L0,10 z", class: `mk mk-${k}` }, m);
    }

    // bands
    const bands = [["cli", 30, 128, "1  CLI"], ["mod", 200, 118, "2  MODULES"], ["ds", 360, 118, "3  STRUCTURES"], ["st", 520, 160, "4  STORAGE"]];
    for (const [k, y, h, name] of bands) {
        el("rect", { class: `band band-${k}`, x: 30, y, width: 1460, height: h, rx: 10 }, svg);
        text(svg, `band-name bn-${k}`, 0, 0, name).setAttribute("transform", `translate(66 ${y + h / 2}) rotate(-90)`);
    }
    el("line", { class: "sep", x1: 1100, y1: 206, x2: 1100, y2: 674 }, svg);
    text(svg, "ptag", 1300, 702, "PLANNED", "middle");

    const parts = [];
    const edges = el("g", {}, svg);
    for (const [kind, d, t, o = {}] of ARCH_EDGES) {
        const g = el("g", { class: o.only ? "ae only" : "ae", "data-t": t }, edges);
        const p = el("path", { d, class: `edge e-${kind}` }, g);
        if (o.end !== false) p.setAttribute("marker-end", `url(#ar-${kind})`);
        if (o.both) p.setAttribute("marker-start", `url(#ar-${kind})`);
        if (o.label) text(g, kind === "file" ? "elbl elbl-file" : "elbl", o.label[0], o.label[1], o.label[2], o.label[3]);
        parts.push(g);
    }

    const byId = {};
    for (const [layer, id, x, y, w, title, sub, mono, t, planned] of ARCH_NODES) {
        const g = el("g", { class: `an an-${layer}${planned ? " planned" : ""}`, "data-id": id, "data-t": t }, svg);
        el("rect", { class: "box", x, y, width: w, height: 62, rx: 8 }, g);
        const cx = x + w / 2;
        const tag = planned && layer === "mod";
        text(g, `nt${mono[0] ? " mono" : ""}`, cx, y + (tag ? 24 : 28), title, "middle");
        text(g, tag ? "ntag" : `ns${mono[1] ? " mono" : ""}`, cx, y + (tag ? 46 : 48), sub, "middle");
        const trace = t.split(" ")[0];
        if (trace !== "core") {
            g.dataset.pick = trace;
            g.addEventListener("click", () => select(current === trace ? null : trace));
        }
        byId[id] = g;
        parts.push(g);
    }
    // module boxes are the keyboard entry point into the diagram
    for (const id of ["m-init", "m-add", "m-ignore", "m-commit", "m-log", "m-checkout", "m-branch"]) {
        const g = byId[id];
        g.setAttribute("tabindex", "0");
        g.setAttribute("role", "button");
        g.setAttribute("aria-label", `Trace ${g.querySelector(".nt").textContent}`);
        g.addEventListener("keydown", (e) => {
            if (e.key === "Enter" || e.key === " ") { e.preventDefault(); g.dispatchEvent(new MouseEvent("click")); }
        });
    }

    const picks = document.querySelectorAll(".arch-pick");
    picks.forEach((b) => b.addEventListener("click", () => select(current === b.dataset.trace ? null : b.dataset.trace)));

    let current = null;
    const steps = $("#archSteps");
    const facts = $("#archFacts");
    const overview = { kicker: $("#archKicker").textContent, title: $("#archTitle").textContent, lead: $("#archLead").textContent };

    function focusNodes(ids) {
        Object.values(byId).forEach((g) => g.classList.toggle("focus", ids.includes(g.dataset.id)));
    }

    function select(name) {
        current = name;
        const tr = name ? ARCH_TRACES[name] : null;
        svg.classList.toggle("tracing", Boolean(tr));
        for (const g of parts) {
            const ts = g.dataset.t.split(" ");
            g.classList.toggle("on", Boolean(tr) && (ts.includes("core") || ts.includes(name)));
            g.classList.toggle("hot", Boolean(tr) && tr.hot.includes(g.dataset.id));
        }
        picks.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.trace === name)));
        focusNodes([]);

        $("#archKicker").textContent = tr ? tr.kicker : overview.kicker;
        $("#archKicker").classList.toggle("planned", Boolean(tr && tr.planned));
        $("#archTitle").textContent = tr ? tr.title : overview.title;
        $("#archTitle").classList.toggle("mono", Boolean(tr));
        $("#archLead").textContent = tr ? tr.lead : overview.lead;
        steps.innerHTML = "";
        facts.innerHTML = "";
        if (!tr) return;
        tr.steps.forEach(([ids, html]) => {
            const li = document.createElement("li");
            li.innerHTML = `<span>${html}</span>`;
            li.tabIndex = 0;
            const on = () => focusNodes(ids);
            const off = () => focusNodes([]);
            li.addEventListener("mouseenter", on);
            li.addEventListener("mouseleave", off);
            li.addEventListener("focus", on);
            li.addEventListener("blur", off);
            steps.appendChild(li);
        });
        for (const [k, v] of tr.facts) {
            const dt = document.createElement("dt");
            dt.textContent = k;
            const dd = document.createElement("dd");
            dd.textContent = v;
            facts.append(dt, dd);
        }
        // on narrow screens the diagram scrolls sideways: bring the traced column into view
        const box = $("#archScroll");
        if (box.scrollWidth > box.clientWidth + 1) {
            const left = (tr.col / 1520) * svg.clientWidth - box.clientWidth / 2;
            box.scrollTo({ left: Math.max(0, left), behavior: reduceMotion.matches ? "auto" : "smooth" });
        }
    }
}

function toast(msg) {
    const t = $("#toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => t.classList.remove("show"), 2200);
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
document.addEventListener("DOMContentLoaded", () => {
    resetRepo();
    renderViews();
    setupTabs();
    setupTheme();
    setupMenu();
    setupObservers();
    setupPdf();
    setupArch();

    const input = $("#cliInput");
    const history = [];
    let pos = 0;
    $("#termForm").addEventListener("submit", (e) => {
        e.preventDefault();
        const v = input.value;
        if (trim(v)) { history.push(v); pos = history.length; }
        input.value = "";
        runCommand(v);
    });
    input.addEventListener("keydown", (e) => {
        if (e.key === "ArrowUp" && pos > 0) { input.value = history[--pos]; e.preventDefault(); }
        else if (e.key === "ArrowDown") { pos = Math.min(history.length, pos + 1); input.value = history[pos] || ""; e.preventDefault(); }
    });

    $("#clearTerminalBtn").addEventListener("click", () => { $("#terminalOutput").textContent = ""; });

    document.querySelectorAll("[data-preset]").forEach((b) => b.addEventListener("click", () => {
        if (b.dataset.preset === "reset") { resetSandbox(true); toast("Sandbox reset"); }
        else playPreset(b.dataset.preset);
    }));

    // Chips run in place; buttons elsewhere on the page also bring the sandbox into view
    document.querySelectorAll("[data-run]").forEach((b) => b.addEventListener("click", () => {
        runCommand(b.dataset.run);
        if (!b.closest("#simulator")) {
            $("#simulator").scrollIntoView({ behavior: reduceMotion.matches ? "auto" : "smooth" });
        }
    }));
});
