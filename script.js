/* ==========================================================================
   MINIGIT — INTERACTIVE STATE ENGINE & MOTION UI CONTROLLER
   ========================================================================== */

// --------------------------------------------------------------------------
// 1. MINIGIT SIMULATOR STATE MACHINE (Complete CLI + Trie + Staging)
// --------------------------------------------------------------------------
class MiniGitSimulator {
    constructor() {
        this.resetState();
    }

    resetState() {
        this.isInitialized = false;
        this.stagedFiles = new Map(); // filename -> { hash, timestamp }
        this.commits = [];           // Array of commit objects (Linked List)
        this.headCommit = null;       // Top commit pointer
        this.rollbackStack = [];     // LIFO Stack for rollback states
        this.commitCounter = 0;
        this.ignoreRules = [];       // array of { pattern, line }
        this.trieNodes = { char: 'ROOT', children: {}, isTerminal: false, line: -1, pattern: '' };
    }

    init() {
        if (this.isInitialized) {
            return { success: false, msg: "Reinitialized existing MiniGit repository in .minigit/" };
        }
        this.isInitialized = true;
        return { success: true, msg: "Initialized empty MiniGit repository in .minigit/" };
    }

    // --- Ignore Module (Trie-based) ---
    ignoreAdd(pattern) {
        if (!this.isInitialized) return { success: false, msg: "fatal: not a minigit repository (run 'minigit init' first)" };
        if (!pattern) return { success: false, msg: "usage: minigit ignore <pattern>\nexample: minigit ignore *.log" };

        pattern = pattern.replace(/\\/g, '/');
        const existing = this.ignoreRules.find(r => r.pattern === pattern);
        if (existing) {
            return { success: true, msg: `'${pattern}' already ignored at line ${existing.line}` };
        }

        const line = this.ignoreRules.length + 1;
        this.ignoreRules.push({ pattern, line });
        this.rebuildTrie();
        return { success: true, msg: `Ignored '${pattern}' (line ${line})` };
    }

    ignoreRemove(pattern) {
        if (!this.isInitialized) return { success: false, msg: "fatal: not a minigit repository" };
        if (!pattern) return { success: false, msg: "usage: minigit ignore -r <pattern>" };

        pattern = pattern.replace(/\\/g, '/');
        const idx = this.ignoreRules.findIndex(r => r.pattern === pattern);
        if (idx === -1) {
            return { success: false, msg: `'${pattern}' is not present in .minigitignore` };
        }
        const removed = this.ignoreRules.splice(idx, 1)[0];
        // Shift line numbers
        this.ignoreRules.forEach((r, i) => r.line = i + 1);
        this.rebuildTrie();
        return { success: true, msg: `Removed '${pattern}' (was line ${removed.line})` };
    }

    ignoreVerify(path) {
        if (!this.isInitialized) return { success: false, msg: "fatal: not a minigit repository" };
        if (!path) return { success: false, msg: "usage: minigit ignore -v <path>" };

        path = path.replace(/\\/g, '/');
        const m = this.checkIgnore(path);
        if (m) {
            return { success: true, msg: `'${path}' is ignored by '${m.pattern}' (line ${m.line})` };
        }
        return { success: true, msg: `'${path}' is not ignored` };
    }

    checkIgnore(path) {
        path = path.replace(/\\/g, '/');
        if (path === '.minigit' || path.startsWith('.minigit/')) {
            return { pattern: '.minigit/', line: 0 };
        }
        for (const r of this.ignoreRules) {
            if (r.pattern.startsWith('*.')) {
                const ext = r.pattern.substring(1);
                if (path.endsWith(ext)) return r;
            } else if (r.pattern.endsWith('/')) {
                if (path.startsWith(r.pattern) || (path + '/').startsWith(r.pattern)) return r;
            } else if (path === r.pattern) {
                return r;
            }
        }
        return null;
    }

    rebuildTrie() {
        this.trieNodes = { char: 'ROOT', children: {}, isTerminal: false, line: -1, pattern: '' };
        for (const r of this.ignoreRules) {
            let key = r.pattern;
            if (key.startsWith('*.')) {
                key = key.substring(1).split('').reverse().join('');
            }
            let curr = this.trieNodes;
            for (const ch of key) {
                if (!curr.children[ch]) {
                    curr.children[ch] = { char: ch, children: {}, isTerminal: false, line: -1, pattern: '' };
                }
                curr = curr.children[ch];
            }
            curr.isTerminal = true;
            curr.line = r.line;
            curr.pattern = r.pattern;
        }
    }

    // --- Staging Module (Index) ---
    add(filename) {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository (run 'minigit init' first)" };
        }
        if (!filename) {
            return { success: false, msg: "usage: minigit add <file> | minigit add ." };
        }

        if (filename === '.') {
            const sampleFiles = ['main.cpp', 'app.h', 'README.md', 'logs/debug.log', 'build/output.o', 'secret.env'];
            let staged = 0, skipped = 0;
            sampleFiles.forEach(f => {
                if (this.checkIgnore(f)) {
                    skipped++;
                } else {
                    const hash = "blob_" + Math.random().toString(36).substring(2, 8);
                    this.stagedFiles.set(f, { hash, timestamp: new Date().toLocaleTimeString() });
                    staged++;
                }
            });
            return { success: true, msg: `staged ${staged} file(s), skipped ${skipped} ignored` };
        }

        filename = filename.replace(/\\/g, '/');
        const ig = this.checkIgnore(filename);
        if (ig) {
            return { success: true, msg: `'${filename}' is ignored by '${ig.pattern}' (line ${ig.line})` };
        }

        if (this.stagedFiles.has(filename)) {
            return { success: true, msg: `'${filename}' is already staged` };
        }

        const hash = "blob_" + Math.random().toString(36).substring(2, 8);
        this.stagedFiles.set(filename, { hash, timestamp: new Date().toLocaleTimeString() });
        return { success: true, msg: `Staged '${filename}'` };
    }

    addVerify(filename) {
        if (!this.isInitialized) return { success: false, msg: "fatal: not a minigit repository" };
        if (!filename) return { success: false, msg: "usage: minigit add -v <filename>" };

        filename = filename.replace(/\\/g, '/');
        if (this.stagedFiles.has(filename)) {
            return { success: true, msg: `'${filename}' is staged` }; // IS_STAGED status
        }
        const ig = this.checkIgnore(filename);
        if (ig) {
            return { success: true, msg: `'${filename}' is ignored by '${ig.pattern}' (line ${ig.line})` };
        }
        return { success: true, msg: `'${filename}' is not staged` };
    }

    remove(filename) {
        if (!this.isInitialized) return { success: false, msg: "fatal: not a minigit repository" };
        if (!filename) return { success: false, msg: "usage: minigit remove <file|.>" };

        if (filename === '.') {
            const count = this.stagedFiles.size;
            this.stagedFiles.clear();
            return { success: true, msg: `Unstaged ${count} files` };
        }
        filename = filename.replace(/\\/g, '/');
        if (!this.stagedFiles.has(filename)) {
            return { success: true, msg: `'${filename}' is not staged` };
        }
        this.stagedFiles.delete(filename);
        return { success: true, msg: `Unstaged '${filename}'` };
    }

    commit(message) {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository" };
        }
        if (this.stagedFiles.size === 0) {
            return { success: false, msg: "nothing to commit, working tree clean" };
        }
        if (!message) {
            return { success: false, msg: "usage: minigit commit -m \"commit message\"" };
        }

        this.commitCounter++;
        const commitId = "c" + this.commitCounter + "_" + Math.random().toString(36).substring(2, 6);
        const timestamp = new Date().toLocaleTimeString();

        const newCommit = {
            id: commitId,
            message: message,
            timestamp: timestamp,
            parent: this.headCommit ? this.headCommit.id : null,
            snapshots: new Map(this.stagedFiles)
        };

        this.commits.push(newCommit);
        this.headCommit = newCommit;
        
        const stagedCount = this.stagedFiles.size;
        this.stagedFiles.clear();

        return { 
            success: true, 
            msg: `[main ${commitId}] ${message}\n ${stagedCount} file(s) changed, snapshots saved to .minigit/objects/` 
        };
    }

    log() {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository" };
        }
        if (this.commits.length === 0) {
            return { success: true, msg: "No commits yet on branch 'main'" };
        }

        let output = "=== Commit History (Singly Linked List Traversal) ===\n\n";
        let curr = this.headCommit;
        
        while (curr) {
            const isHead = curr.id === this.headCommit.id ? " (HEAD -> main)" : "";
            output += `commit ${curr.id}${isHead}\n`;
            output += `Date:   ${curr.timestamp}\n`;
            output += `Parent: ${curr.parent ? curr.parent : 'root'}\n`;
            output += `    ${curr.message}\n\n`;
            
            curr = this.commits.find(c => c.id === curr.parent);
        }

        return { success: true, msg: output.trim() };
    }

    checkout(targetCommitId) {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository" };
        }
        if (!targetCommitId) {
            return { success: false, msg: "usage: minigit checkout <commit-id>" };
        }

        const targetCommit = this.commits.find(c => c.id === targetCommitId) ||
                             this.commits.find(c => c.id.split('_')[0] === targetCommitId) ||
                             this.commits.find(c => c.id.startsWith(targetCommitId));
        if (!targetCommit) {
            return { success: false, msg: `error: pathspec '${targetCommitId}' did not match any commit` };
        }

        this.rollbackStack.push({
            previousHead: this.headCommit,
            timestamp: new Date().toLocaleTimeString(),
            description: `Checkout to ${targetCommit.id}`
        });

        this.headCommit = targetCommit;
        return { success: true, msg: `HEAD is now at ${targetCommit.id} (${targetCommit.message})\nPushed pre-checkout state to Rollback Stack.` };
    }

    rollback() {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository" };
        }
        if (this.rollbackStack.length === 0) {
            return { success: false, msg: "rollback stack is empty: nothing to undo" };
        }

        const topState = this.rollbackStack.pop();
        this.headCommit = topState.previousHead;
        return { success: true, msg: `Rollback successful! Restored HEAD to ${this.headCommit.id} (${this.headCommit.message})\nPopped state off Rollback Stack.` };
    }
}

// Instantiate global simulator
const repo = new MiniGitSimulator();

// --------------------------------------------------------------------------
// 2. DOM INTERACTION & CLI INTERPRETER
// --------------------------------------------------------------------------
document.addEventListener('DOMContentLoaded', () => {
    const cliInput = document.getElementById('cliInput');
    const cliSendBtn = document.getElementById('cliSendBtn');
    const terminalOutput = document.getElementById('terminalOutput');
    const clearTerminalBtn = document.getElementById('clearTerminalBtn');
    const themeToggle = document.getElementById('themeToggle');

    let cmdHistory = [];
    let historyIndex = -1;

    function processCommand() {
        const rawInput = cliInput.value.trim();
        if (!rawInput) return;

        cmdHistory.push(rawInput);
        historyIndex = cmdHistory.length;

        printTermLine(`minigit $ ${rawInput}`, 'cmd');
        cliInput.value = '';

        executeCommandString(rawInput);

        const termBody = document.getElementById('terminalBody');
        termBody.scrollTop = termBody.scrollHeight;
    }

    if (cliSendBtn) cliSendBtn.addEventListener('click', processCommand);
    if (cliInput) {
        cliInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                processCommand();
            } else if (e.key === 'ArrowUp') {
                if (historyIndex > 0) {
                    historyIndex--;
                    cliInput.value = cmdHistory[historyIndex];
                }
            } else if (e.key === 'ArrowDown') {
                if (historyIndex < cmdHistory.length - 1) {
                    historyIndex++;
                    cliInput.value = cmdHistory[historyIndex];
                } else {
                    historyIndex = cmdHistory.length;
                    cliInput.value = '';
                }
            }
        });
    }

    if (clearTerminalBtn) {
        clearTerminalBtn.addEventListener('click', () => {
            terminalOutput.innerHTML = '';
            printTermLine("Terminal cleared.", 'info');
        });
    }

    // Tab Switching for Visualizers (Accessible ARIA compliant)
    const vizTabs = document.querySelectorAll('.viz-tab');
    vizTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            vizTabs.forEach(t => {
                t.classList.remove('active');
                t.setAttribute('aria-selected', 'false');
            });
            document.querySelectorAll('.viz-view').forEach(v => v.classList.remove('active'));

            tab.classList.add('active');
            tab.setAttribute('aria-selected', 'true');
            const targetId = 'viz-' + tab.dataset.tab;
            const targetView = document.getElementById(targetId);
            if (targetView) targetView.classList.add('active');
        });
    });

    // Theme Toggle Listener
    if (themeToggle) {
        const savedTheme = localStorage.getItem('minigit-theme');
        if (savedTheme === 'light') {
            document.documentElement.classList.add('light-theme');
            themeToggle.innerHTML = '<i class="fa-solid fa-sun"></i>';
        }

        themeToggle.addEventListener('click', () => {
            document.documentElement.classList.toggle('light-theme');
            const isLight = document.documentElement.classList.contains('light-theme');
            themeToggle.innerHTML = isLight ? '<i class="fa-solid fa-sun"></i>' : '<i class="fa-solid fa-moon"></i>';
            localStorage.setItem('minigit-theme', isLight ? 'light' : 'dark');
        });
    }

    updateVisualizers();
    setupPdfViewer();
    setupMobileMenu();
    setupScrollSpy();
});

function printTermLine(text, type = 'info') {
    const output = document.getElementById('terminalOutput');
    if (!output) return;

    const line = document.createElement('div');
    line.className = `term-line ${type}`;

    if (text.includes('\n')) {
        line.innerHTML = `<pre>${escapeHtml(text)}</pre>`;
    } else {
        line.textContent = text;
    }

    output.appendChild(line);
    scrollTerminalToBottom();
}

function scrollTerminalToBottom() {
    const termBody = document.getElementById('terminalBody');
    if (termBody) {
        requestAnimationFrame(() => {
            termBody.scrollTop = termBody.scrollHeight;
        });
    }
}

function escapeHtml(str) {
    return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// Parse Command String
function executeCommandString(cmdStr) {
    let parts = cmdStr.trim().split(/\s+/);
    
    if (parts[0] === 'minigit' || parts[0] === './minigit' || parts[0] === '.\\minigit') {
        parts.shift();
    }

    const command = parts[0] ? parts[0].toLowerCase() : '';

    if (command === 'help' || command === '--help') {
        printTermLine(
`MiniGit Command Reference:
  minigit init                    - Initialize local .minigit/ repository
  minigit add <file> | add .      - Stage files to index (respects .minigitignore)
  minigit add -v <file>           - Verify if file is staged (IS_STAGED status)
  minigit remove <file|.>         - Unstage file(s) from index
  minigit ignore <pattern>        - Add pattern to .minigitignore (Trie indexed)
  minigit ignore -r <pattern>     - Remove pattern from .minigitignore
  minigit ignore -v <path>        - Check if ignored & return line number
  minigit commit -m "<msg>"       - Create commit snapshot node
  minigit log                     - Traverse commit linked list history
  minigit checkout <commit-id>    - Restore version snapshot
  minigit rollback                - Undo last checkout via Stack
  minigit install                 - Auto-install to user PATH
  clear                           - Clear terminal`, 'info');
        return;
    }

    if (command === 'clear') {
        document.getElementById('terminalOutput').innerHTML = '';
        return;
    }

    let result;
    switch (command) {
        case 'init':
            result = repo.init();
            break;

        case 'add':
            if (parts[1] === '-v' || parts[1] === '--verify') {
                result = repo.addVerify(parts[2]);
            } else {
                result = repo.add(parts[1]);
            }
            break;

        case 'remove':
        case 'rm':
            result = repo.remove(parts[1]);
            break;

        case 'ignore':
            if (parts[1] === '-r' || parts[1] === '--remove') {
                result = repo.ignoreRemove(parts[2]);
            } else if (parts[1] === '-v' || parts[1] === '--verify') {
                result = repo.ignoreVerify(parts[2]);
            } else if (parts[1]) {
                result = repo.ignoreAdd(parts[1]);
            } else {
                if (repo.ignoreRules.length === 0) {
                    result = { success: true, msg: "No patterns in .minigitignore" };
                } else {
                    const list = repo.ignoreRules.map(r => `${r.line}: ${r.pattern}`).join('\n');
                    result = { success: true, msg: list };
                }
            }
            break;

        case 'commit':
            const mIndex = parts.indexOf('-m');
            if (mIndex === -1) {
                result = { success: false, msg: "error: missing -m flag\nusage: minigit commit -m \"commit message\"" };
            } else {
                const commitMsg = parts.slice(mIndex + 1).join(' ').replace(/^["']|["']$/g, '').trim();
                if (!commitMsg) {
                    result = { success: false, msg: "error: commit message cannot be empty\nusage: minigit commit -m \"commit message\"" };
                } else {
                    result = repo.commit(commitMsg);
                }
            }
            break;

        case 'log':
            result = repo.log();
            break;

        case 'checkout':
            result = repo.checkout(parts[1]);
            break;

        case 'rollback':
            result = repo.rollback();
            break;

        case 'install':
            result = {
                success: true,
                msg: "Successfully installed MiniGit!\n  Installed binary to: %LOCALAPPDATA%\\Microsoft\\WindowsApps\\minigit.exe\nYou can now run 'minigit' directly without '.\\'."
            };
            break;

        default:
            result = { success: false, msg: `minigit: '${command}' is not a minigit command. Type 'help' for usage.` };
            break;
    }

    if (result) {
        printTermLine(result.msg, result.success ? 'success' : 'error');
    }

    updateVisualizers();
}

// --------------------------------------------------------------------------
// 3. VISUALIZER RENDERING ENGINE
// --------------------------------------------------------------------------
function updateVisualizers() {
    updateLinkedListVisualizer();
    updateHashTableVisualizer();
    updateTrieVisualizer();
    updateStackVisualizer();
    updateFileSystemVisualizer();
}

function updateLinkedListVisualizer() {
    const container = document.getElementById('linkedListGraph');
    const badge = document.getElementById('commitCountBadge');
    if (!container) return;

    if (!repo.isInitialized || repo.commits.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-box-open"></i>
                <p>Repository not initialized or no commits yet.</p>
                <span>Type <code>minigit init</code> and create your first commit!</span>
            </div>`;
        if (badge) badge.textContent = '0 Commits';
        return;
    }

    if (badge) badge.textContent = `${repo.commits.length} Commit(s)`;

    let html = '';
    const reversed = [...repo.commits].reverse();

    reversed.forEach((c, index) => {
        const isHead = repo.headCommit && repo.headCommit.id === c.id;
        const headBadge = isHead ? '<span class="node-head-badge"><i class="fa-solid fa-crosshairs"></i> HEAD</span>' : '';
        const nodeClass = isHead ? 'commit-node head-node' : 'commit-node';

        html += `
            <div class="${nodeClass}">
                <div class="node-header">
                    <span class="commit-id"><i class="fa-solid fa-code-commit"></i> ${c.id}</span>
                    ${headBadge}
                </div>
                <div class="node-body">
                    <p class="node-msg">"${escapeHtml(c.message)}"</p>
                    <span class="node-time"><i class="fa-regular fa-clock"></i> ${c.timestamp}</span>
                    <span class="node-parent"><i class="fa-solid fa-arrow-left"></i> Parent: ${c.parent ? c.parent : 'NULL (root)'}</span>
                </div>
            </div>
        `;

        if (index < reversed.length - 1) {
            html += `<div class="node-pointer"><i class="fa-solid fa-arrow-right"></i></div>`;
        }
    });

    container.innerHTML = html;
}

function updateHashTableVisualizer() {
    const container = document.getElementById('hashTableGrid');
    const badge = document.getElementById('stagedCountBadge');
    if (!container) return;

    if (!repo.isInitialized || repo.stagedFiles.size === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-hashtag"></i>
                <p>Staging area (.minigit/index) is currently empty.</p>
                <span>Run <code>minigit add &lt;file&gt;</code> or <code>minigit add .</code> to stage files.</span>
            </div>`;
        if (badge) badge.textContent = '0 Files Staged';
        return;
    }

    if (badge) badge.textContent = `${repo.stagedFiles.size} File(s) in Index`;

    let html = '<div class="table-responsive"><table class="data-table"><thead><tr><th>File Path (Index Entry)</th><th>Content Hash (SHA Blob)</th><th>Staged At</th></tr></thead><tbody>';

    repo.stagedFiles.forEach((val, file) => {
        html += `
            <tr>
                <td><code><i class="fa-regular fa-file-lines text-cyan"></i> ${escapeHtml(file)}</code></td>
                <td><span class="hash-badge">${val.hash}</span></td>
                <td><small>${val.timestamp}</small></td>
            </tr>
        `;
    });

    html += '</tbody></table></div>';
    container.innerHTML = html;
}

function updateTrieVisualizer() {
    const container = document.getElementById('trieGraph');
    const badge = document.getElementById('trieCountBadge');
    if (!container) return;

    if (!repo.isInitialized) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-network-wired"></i>
                <p>Repository not initialized.</p>
                <span>Run <code>minigit init</code> and add rules using <code>minigit ignore &lt;pattern&gt;</code>.</span>
            </div>`;
        if (badge) badge.textContent = '0 Rules';
        return;
    }

    if (badge) badge.textContent = `${repo.ignoreRules.length} Rule(s) in Trie`;

    if (repo.ignoreRules.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-network-wired"></i>
                <p>No patterns in .minigitignore.</p>
                <span>Try <code>minigit ignore *.log</code>, <code>minigit ignore build/</code>, or <code>minigit ignore secret.txt</code>.</span>
            </div>`;
        return;
    }

    let html = '<div class="trie-viz-wrapper">';
    
    // Left: Rules in .minigitignore
    html += '<div class="trie-rules-panel"><h4><i class="fa-regular fa-file-code text-cyan"></i> .minigitignore Rules</h4><div class="trie-rule-list">';
    repo.ignoreRules.forEach(r => {
        const type = r.pattern.startsWith('*.') ? 'Reversed Ext Trie' : (r.pattern.endsWith('/') ? 'Directory Prefix Trie' : 'Exact Path Trie');
        html += `
            <div class="trie-rule-badge">
                <span class="rule-line-no">Line ${r.line}</span>
                <code class="rule-code">${escapeHtml(r.pattern)}</code>
                <span class="rule-tag">${type}</span>
            </div>
        `;
    });
    html += '</div></div>';

    // Right: Visual Prefix Tree Nodes
    html += '<div class="trie-nodes-panel"><h4><i class="fa-solid fa-sitemap text-purple"></i> Active Trie Node Graph (Prefix Matching)</h4><div class="trie-tree-root">';
    html += renderTrieNode(repo.trieNodes);
    html += '</div></div>';

    html += '</div>';
    container.innerHTML = html;
}

function renderTrieNode(node) {
    if (!node) return '';
    const childKeys = Object.keys(node.children);
    let childHtml = '';
    if (childKeys.length > 0) {
        childHtml += '<div class="trie-children-container">';
        for (const k of childKeys) {
            childHtml += renderTrieNode(node.children[k]);
        }
        childHtml += '</div>';
    }

    const isTerm = node.isTerminal ? 'trie-terminal' : '';
    const matchBadge = node.isTerminal ? `<span class="trie-match-pill"><i class="fa-solid fa-ban"></i> Line ${node.line}: ${node.pattern}</span>` : '';

    return `
        <div class="trie-node-wrapper ${isTerm}">
            <div class="trie-node-chip">
                <span class="trie-char">${node.char}</span>
                ${matchBadge}
            </div>
            ${childHtml}
        </div>
    `;
}

function updateStackVisualizer() {
    const container = document.getElementById('stackContainer');
    const badge = document.getElementById('stackDepthBadge');
    if (!container) return;

    if (!repo.isInitialized || repo.rollbackStack.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-layer-group"></i>
                <p>Rollback stack is empty.</p>
                <span>Executing <code>minigit checkout &lt;commit-id&gt;</code> pushes pre-checkout state onto the stack.</span>
            </div>`;
        if (badge) badge.textContent = 'Stack Depth: 0';
        return;
    }

    if (badge) badge.textContent = `Stack Depth: ${repo.rollbackStack.length}`;

    let html = '<div class="stack-list">';
    const stackReversed = [...repo.rollbackStack].reverse();

    stackReversed.forEach((s, idx) => {
        const isTop = idx === 0 ? '<span class="stack-top-badge"><i class="fa-solid fa-arrow-down"></i> TOP OF STACK</span>' : '';
        html += `
            <div class="stack-item ${idx === 0 ? 'top-item' : ''}">
                <div class="stack-item-header">
                    <span><strong>Frame #${repo.rollbackStack.length - idx}</strong>: ${s.description}</span>
                    ${isTop}
                </div>
                <div class="stack-item-body">
                    <span>Target Head: <code>${s.previousHead ? s.previousHead.id : 'root'}</code></span>
                    <small>${s.timestamp}</small>
                </div>
            </div>
        `;
    });

    html += '</div>';
    container.innerHTML = html;
}

function updateFileSystemVisualizer() {
    const container = document.getElementById('fileTreeContainer');
    const badge = document.getElementById('fsStatusBadge');
    if (!container) return;

    if (!repo.isInitialized) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-folder-closed"></i>
                <p>No <code>.minigit/</code> directory found.</p>
                <span>Run <code>minigit init</code> to generate storage structure.</span>
            </div>`;
        if (badge) badge.textContent = 'Not Initialized';
        return;
    }

    if (badge) badge.textContent = 'Active (.minigit)';

    let objectCount = repo.commits.length;
    let headStr = repo.headCommit ? repo.headCommit.id : 'ref: refs/heads/main';
    let stagedCount = repo.stagedFiles.size;
    let ignoreCount = repo.ignoreRules.length;

    container.innerHTML = `
        <div class="tree-item folder"><i class="fa-solid fa-folder-open text-cyan"></i> .minigit/</div>
        <div class="tree-item folder" style="padding-left:20px;"><i class="fa-solid fa-folder-open text-amber"></i> objects/ (${objectCount} commit snapshots)</div>
        <div class="tree-item file" style="padding-left:38px;"><i class="fa-solid fa-file-code text-purple"></i> index (${stagedCount} paths staged)</div>
        <div class="tree-item file" style="padding-left:38px;"><i class="fa-solid fa-tag text-emerald"></i> HEAD -> [${headStr}]</div>
        <div class="tree-item file" style="padding-left:20px;"><i class="fa-solid fa-file-shield text-red"></i> .minigitignore (${ignoreCount} rules loaded in Trie)</div>
    `;
}

// --------------------------------------------------------------------------
// 4. PRESETS & ACTIONS
// --------------------------------------------------------------------------
function executeInSandbox(cmd) {
    const input = document.getElementById('cliInput');
    if (input) {
        input.value = cmd;
        document.getElementById('cliSendBtn').click();
    }
    const simSection = document.getElementById('simulator');
    if (simSection) {
        simSection.scrollIntoView({ behavior: 'smooth' });
    }
}

function copyText(text, btnElement = null) {
    if (!btnElement && window.event && window.event.currentTarget) {
        btnElement = window.event.currentTarget;
    }

    navigator.clipboard.writeText(text).then(() => {
        if (btnElement) {
            const originalHtml = btnElement.innerHTML;
            btnElement.innerHTML = '<i class="fa-solid fa-check text-emerald"></i> Copied!';
            btnElement.classList.add('copied');
            setTimeout(() => {
                btnElement.innerHTML = originalHtml;
                btnElement.classList.remove('copied');
            }, 1800);
        }
        showToast(`Copied "${text}" to clipboard!`);
    }).catch(err => {
        console.error("Failed to copy", err);
        showToast(`Copied "${text}"`);
    });
}

function showToast(msg) {
    const existing = document.querySelector('.minigit-toast');
    if (existing) existing.remove();

    const toast = document.createElement('div');
    toast.className = 'minigit-toast';
    toast.innerHTML = `<i class="fa-solid fa-circle-check text-cyan"></i> <span>${escapeHtml(msg)}</span>`;
    document.body.appendChild(toast);

    requestAnimationFrame(() => toast.classList.add('show'));
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => toast.remove(), 300);
    }, 2400);
}

let activePresetTimers = [];

function clearActivePresetTimers() {
    activePresetTimers.forEach(id => clearTimeout(id));
    activePresetTimers = [];
    setPresetButtonsLoading(false);
}

function setPresetButtonsLoading(running, activeBtn = null) {
    const presetBtns = document.querySelectorAll('.btn-preset:not(.btn-reset)');
    presetBtns.forEach(b => {
        if (running) {
            b.disabled = true;
            b.style.pointerEvents = 'none';
            b.style.opacity = '0.6';
        } else {
            b.disabled = false;
            b.style.pointerEvents = '';
            b.style.opacity = '';
            if (b.dataset.origHtml) {
                b.innerHTML = b.dataset.origHtml;
                delete b.dataset.origHtml;
            }
        }
    });

    if (activeBtn && running) {
        activeBtn.dataset.origHtml = activeBtn.innerHTML;
        activeBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Running...';
    }
}

function schedulePresetStep(cmd, delay, isFinal = false) {
    const timerId = setTimeout(() => {
        executeCommandString(cmd);
        activePresetTimers = activePresetTimers.filter(id => id !== timerId);
        if (isFinal) {
            setPresetButtonsLoading(false);
        }
    }, delay);
    activePresetTimers.push(timerId);
}

function runPresetWorkflow(type, event = null) {
    let clickedBtn = null;
    if (event && event.currentTarget) clickedBtn = event.currentTarget;
    else if (window.event && window.event.currentTarget) clickedBtn = window.event.currentTarget;

    clearActivePresetTimers();
    resetSandbox(false);
    setPresetButtonsLoading(true, clickedBtn);
    
    if (type === 'full') {
        executeCommandString('minigit init');
        schedulePresetStep('minigit ignore *.log', 300);
        schedulePresetStep('minigit ignore build/', 600);
        schedulePresetStep('minigit add main.cpp', 900);
        schedulePresetStep('minigit add -v main.cpp', 1200);
        schedulePresetStep('minigit add .', 1500);
        schedulePresetStep('minigit commit -m "feat: initial commit with Trie ignore engine"', 1900, true);
    } else if (type === 'ignore_demo') {
        executeCommandString('minigit init');
        schedulePresetStep('minigit ignore *.log', 300);
        schedulePresetStep('minigit ignore build/', 600);
        schedulePresetStep('minigit ignore secret.txt', 900);
        schedulePresetStep('minigit ignore -v build/output.o', 1200);
        schedulePresetStep('minigit ignore -v app.log', 1500);
        schedulePresetStep('minigit add app.log', 1800, true);
    } else if (type === 'checkout_demo') {
        executeCommandString('minigit init');
        schedulePresetStep('minigit add file1.txt', 300);
        schedulePresetStep('minigit commit -m "Commit 1"', 600);
        schedulePresetStep('minigit add file2.txt', 900);
        schedulePresetStep('minigit commit -m "Commit 2"', 1200);
        schedulePresetStep('minigit checkout c1', 1600, true);
    } else if (type === 'init_add_commit') {
        executeCommandString('minigit init');
        schedulePresetStep('minigit add main.cpp', 300);
        schedulePresetStep('minigit add -v main.cpp', 600);
        schedulePresetStep('minigit commit -m "Initial commit"', 1000, true);
    }
}

function resetSandbox(notify = true) {
    clearActivePresetTimers();
    repo.resetState();
    const termOutput = document.getElementById('terminalOutput');
    if (termOutput) termOutput.innerHTML = '';
    if (notify) {
        printTermLine("Repository reset. Ready for new operations.", 'warning');
    }
    updateVisualizers();
}

function setupPdfViewer() {
    const pdfFrame = document.getElementById('pdfFrame');
    const pdfTabs = document.querySelectorAll('.pdf-tab');
    const pdfTitle = document.getElementById('pdfDocTitle');
    const pdfDesc = document.getElementById('pdfDocDesc');
    const pdfDownload = document.getElementById('pdfDownloadBtn');
    const pdfOpenTab = document.getElementById('pdfOpenTabBtn');
    const pdfFullscreen = document.getElementById('pdfFullscreenBtn');
    const pdfContainer = document.getElementById('pdfViewerContainer');

    const docs = {
        report: {
            file: 'Reports/report.pdf',
            title: 'MiniGit Phase 1 Design Report',
            desc: 'Comprehensive specification covering problem motivation, system architecture, data structure analysis, class designs, and execution flows.',
            downloadName: 'MiniGit-Phase1-Report.pdf'
        },
        ppt: {
            file: 'Reports/ppt.pdf',
            title: 'MiniGit Phase 1 Presentation Deck',
            desc: 'Official presentation deck outlining problem motivation, modular 5-layer design, command set, and roadmap for Phase 1.',
            downloadName: 'MiniGit-Phase1-Presentation.pdf'
        }
    };

    pdfTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const docKey = tab.dataset.doc;
            if (!docs[docKey]) return;

            pdfTabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            const doc = docs[docKey];
            if (pdfFrame) pdfFrame.src = `${doc.file}#toolbar=1&view=FitH`;
            if (pdfTitle) pdfTitle.textContent = doc.title;
            if (pdfDesc) pdfDesc.textContent = doc.desc;
            if (pdfDownload) {
                pdfDownload.href = doc.file;
                pdfDownload.download = doc.downloadName;
            }
            if (pdfOpenTab) pdfOpenTab.href = doc.file;
        });
    });

    if (pdfFullscreen && pdfContainer) {
        pdfFullscreen.addEventListener('click', () => {
            if (!document.fullscreenElement) {
                pdfContainer.requestFullscreen().catch(err => console.warn(err));
            } else {
                document.exitFullscreen();
            }
        });
    }
}

function setupMobileMenu() {
    const toggleBtn = document.getElementById('mobileMenuToggle');
    const navMenu = document.getElementById('primaryNav');
    const header = document.getElementById('siteHeader');
    if (!toggleBtn || !navMenu) return;

    function closeMenu() {
        navMenu.classList.remove('open');
        toggleBtn.setAttribute('aria-expanded', 'false');
        toggleBtn.innerHTML = '<i class="fa-solid fa-bars"></i>';
    }

    function openMenu() {
        navMenu.classList.add('open');
        toggleBtn.setAttribute('aria-expanded', 'true');
        toggleBtn.innerHTML = '<i class="fa-solid fa-xmark"></i>';
    }

    toggleBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        const isOpen = navMenu.classList.contains('open');
        if (isOpen) {
            closeMenu();
        } else {
            openMenu();
        }
    });

    // Close when clicking outside of header
    document.addEventListener('click', (e) => {
        if (header && !header.contains(e.target)) {
            closeMenu();
        }
    });

    // Close on Escape key
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            closeMenu();
        }
    });

    navMenu.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', () => {
            closeMenu();
        });
    });
}

function setupScrollSpy() {
    const header = document.getElementById('siteHeader');
    const navLinks = document.querySelectorAll('.nav-link[data-section]');
    const sections = Array.from(navLinks)
        .map(link => document.getElementById(link.dataset.section))
        .filter(Boolean);

    // Dynamic Island Header elevation on scroll
    let ticking = false;
    window.addEventListener('scroll', () => {
        if (!ticking) {
            window.requestAnimationFrame(() => {
                if (header) {
                    if (window.scrollY > 25) {
                        header.classList.add('scrolled');
                    } else {
                        header.classList.remove('scrolled');
                    }
                }
                ticking = false;
            });
            ticking = true;
        }
    }, { passive: true });

    // Smooth header offset scrolling for nav links
    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            const secId = link.dataset.section;
            const targetSec = document.getElementById(secId);
            if (targetSec) {
                e.preventDefault();
                const headerOffset = 84;
                const elementPosition = targetSec.getBoundingClientRect().top;
                const offsetPosition = elementPosition + window.pageYOffset - headerOffset;

                window.scrollTo({
                    top: offsetPosition,
                    behavior: 'smooth'
                });

                // Update active immediately on click
                navLinks.forEach(l => {
                    l.classList.remove('active');
                    l.removeAttribute('aria-current');
                });
                link.classList.add('active');
                link.setAttribute('aria-current', 'page');
            }
        });
    });

    // IntersectionObserver for tracking active navigation link
    if ('IntersectionObserver' in window && sections.length > 0) {
        const observerOptions = {
            root: null,
            rootMargin: '-20% 0px -55% 0px',
            threshold: 0
        };

        const observer = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if (entry.isIntersecting) {
                    const sectionId = entry.target.id;
                    navLinks.forEach(link => {
                        if (link.dataset.section === sectionId) {
                            link.classList.add('active');
                            link.setAttribute('aria-current', 'page');
                        } else {
                            link.classList.remove('active');
                            link.removeAttribute('aria-current');
                        }
                    });
                }
            });
        }, observerOptions);

        sections.forEach(sec => observer.observe(sec));
    }
}
