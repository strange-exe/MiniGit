/* ==========================================================================
   MINIGIT — INTERACTIVE STATE ENGINE & MOTION UI CONTROLLER
   ========================================================================== */

// --------------------------------------------------------------------------
// 1. MINIGIT SIMULATOR STATE MACHINE
// --------------------------------------------------------------------------
class MiniGitSimulator {
    constructor() {
        this.resetState();
    }

    resetState() {
        this.isInitialized = false;
        this.stagedFiles = new Map(); // filename -> blobHash
        this.commits = [];           // Array of commit objects (Linked List representation)
        this.headCommit = null;       // Reference to top commit
        this.rollbackStack = [];     // LIFO Stack for rollback states
        this.commitCounter = 0;
    }

    init() {
        if (this.isInitialized) {
            return { success: false, msg: "Reinitialized existing MiniGit repository in .minigit/" };
        }
        this.isInitialized = true;
        return { success: true, msg: "Initialized empty MiniGit repository in .minigit/" };
    }

    add(filename, content = "std::cout << \"Hello MiniGit\";") {
        if (!this.isInitialized) {
            return { success: false, msg: "fatal: not a minigit repository (run 'minigit init' first)" };
        }
        if (!filename) {
            return { success: false, msg: "usage: minigit add <filename>" };
        }
        
        // Simple hash calculation simulation
        const hash = "blob_" + Math.random().toString(36).substring(2, 8);
        this.stagedFiles.set(filename, { hash, content, timestamp: new Date().toLocaleTimeString() });
        return { success: true, msg: `staged '${filename}' [${hash}]` };
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

        // Create new commit node linking to previous HEAD
        const newCommit = {
            id: commitId,
            message: message,
            timestamp: timestamp,
            parent: this.headCommit ? this.headCommit.id : null,
            snapshots: new Map(this.stagedFiles)
        };

        this.commits.push(newCommit);
        this.headCommit = newCommit;
        
        // Clear staging index after commit
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
            
            // Find parent node in commits array
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

        const targetCommit = this.commits.find(c => c.id === targetCommitId || c.id.startsWith(targetCommitId));
        if (!targetCommit) {
            return { success: false, msg: `error: pathspec '${targetCommitId}' did not match any commit` };
        }

        // Push current state onto Rollback Stack before restoring
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

        // Pop from Stack
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

    // Command History Memory
    let cmdHistory = [];
    let historyIndex = -1;

    // Send Command Event
    function processCommand() {
        const rawInput = cliInput.value.trim();
        if (!rawInput) return;

        // Add to history
        cmdHistory.push(rawInput);
        historyIndex = cmdHistory.length;

        // Print input line to terminal
        printTermLine(`minigit $ ${rawInput}`, 'cmd');
        cliInput.value = '';

        // Execute Command Logic
        executeCommandString(rawInput);

        // Auto Scroll Terminal to bottom
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

    // Tab Switching for Visualizers
    const vizTabs = document.querySelectorAll('.viz-tab');
    vizTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            vizTabs.forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.viz-view').forEach(v => v.classList.remove('active'));

            tab.classList.add('active');
            const targetId = 'viz-' + tab.dataset.tab;
            const targetView = document.getElementById(targetId);
            if (targetView) targetView.classList.add('active');
        });
    });

    // Theme Toggle Listener
    if (themeToggle) {
        // Load saved theme preference
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

    // Initial Visualizer Render
    updateVisualizers();
});

// Helper to output to terminal
function printTermLine(text, type = 'info') {
    const output = document.getElementById('terminalOutput');
    if (!output) return;

    const line = document.createElement('div');
    line.className = `term-line ${type}`;

    // Handle multiline output
    if (text.includes('\n')) {
        line.innerHTML = `<pre>${escapeHtml(text)}</pre>`;
    } else {
        line.textContent = text;
    }

    output.appendChild(line);
}

function escapeHtml(str) {
    return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// Parse Command String
function executeCommandString(cmdStr) {
    let parts = cmdStr.trim().split(/\s+/);
    
    // Handle 'minigit' prefix if typed
    if (parts[0] === 'minigit' || parts[0] === './minigit') {
        parts.shift();
    }

    const command = parts[0] ? parts[0].toLowerCase() : '';

    if (command === 'help') {
        printTermLine(
`Available MiniGit Commands:
  minigit init                    - Create empty repository in .minigit/
  minigit add <file>              - Track file in Hash Table index
  minigit commit -m "<msg>"       - Create timestamped snapshot linked list node
  minigit log                     - Traverse commit linked list history
  minigit checkout <commit-id>    - Restore snapshot & push to Rollback Stack
  minigit rollback                - Pop Stack and undo last checkout
  clear                           - Clear terminal screen`, 'info');
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
            const filename = parts[1] || 'main.cpp';
            result = repo.add(filename);
            break;

        case 'commit':
            let msg = "Snapshot update";
            const mIndex = parts.indexOf('-m');
            if (mIndex !== -1 && parts[mIndex + 1]) {
                msg = parts.slice(mIndex + 1).join(' ').replace(/^["']|["']$/g, '');
            } else if (parts[1]) {
                msg = parts.slice(1).join(' ');
            }
            result = repo.commit(msg);
            break;

        case 'log':
            result = repo.log();
            break;

        case 'checkout':
            const targetId = parts[1];
            result = repo.checkout(targetId);
            break;

        case 'rollback':
            result = repo.rollback();
            break;

        default:
            result = { success: false, msg: `minigit: '${command}' is not a minigit command. See 'help'.` };
            break;
    }

    printTermLine(result.msg, result.success ? 'success' : 'error');
    updateVisualizers();
}

// --------------------------------------------------------------------------
// 3. LIVE DATA STRUCTURE RENDERERS
// --------------------------------------------------------------------------
function updateVisualizers() {
    renderLinkedList();
    renderHashTable();
    renderStack();
    renderFileTree();
}

// View 1: Singly Linked List (Commits)
function renderLinkedList() {
    const container = document.getElementById('linkedListGraph');
    const badge = document.getElementById('commitCountBadge');
    if (!container) return;

    if (badge) badge.textContent = `${repo.commits.length} Commit(s)`;

    if (repo.commits.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-box-open"></i>
                <p>Repository not initialized or no commits yet.</p>
                <span>Type <code>minigit init</code> and create your first commit!</span>
            </div>`;
        return;
    }

    container.innerHTML = '';

    // Render commits in reverse chronological order (HEAD at top)
    for (let i = repo.commits.length - 1; i >= 0; i--) {
        const c = repo.commits[i];
        const isHead = repo.headCommit && c.id === repo.headCommit.id;

        const node = document.createElement('div');
        node.className = `commit-node ${isHead ? 'head-node' : ''}`;
        node.innerHTML = `
            <div>
                <span class="commit-id">${c.id}</span>
                <div class="commit-msg">${escapeHtml(c.message)}</div>
                <small style="color:var(--text-muted); font-size:0.75rem;">${c.timestamp}</small>
            </div>
            ${isHead ? '<span class="head-tag"><i class="fa-solid fa-location-dot"></i> HEAD</span>' : ''}
        `;

        container.appendChild(node);

        // Render linking arrow to parent node if parent exists
        if (c.parent) {
            const arrow = document.createElement('div');
            arrow.className = 'commit-arrow';
            arrow.innerHTML = '<i class="fa-solid fa-arrow-down"></i> <span style="font-size:0.75rem;">(points to parent pointer)</span>';
            container.appendChild(arrow);
        }
    }
}

// View 2: Hash Table (Staging & Blobs)
function renderHashTable() {
    const grid = document.getElementById('hashTableGrid');
    const badge = document.getElementById('stagedCountBadge');
    if (!grid) return;

    if (badge) badge.textContent = `${repo.stagedFiles.size} File(s) Staged`;

    if (repo.stagedFiles.size === 0) {
        grid.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-hashtag"></i>
                <p>Staging area (index) is currently empty.</p>
                <span>Run <code>minigit add &lt;file&gt;</code> to stage files into the Hash Table.</span>
            </div>`;
        return;
    }

    grid.innerHTML = '';
    repo.stagedFiles.forEach((val, key) => {
        const row = document.createElement('div');
        row.className = 'hash-row';
        row.innerHTML = `
            <span class="hash-key"><i class="fa-solid fa-file-code"></i> ${escapeHtml(key)}</span>
            <span class="hash-value"><i class="fa-solid fa-fingerprint"></i> ${val.hash}</span>
        `;
        grid.appendChild(row);
    });
}

// View 3: Rollback Stack
function renderStack() {
    const container = document.getElementById('stackContainer');
    const badge = document.getElementById('stackDepthBadge');
    if (!container) return;

    if (badge) badge.textContent = `Stack Depth: ${repo.rollbackStack.length}`;

    if (repo.rollbackStack.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-layer-group"></i>
                <p>Rollback stack is empty.</p>
                <span>Executing <code>minigit checkout &lt;commit-id&gt;</code> pushes current state onto stack.</span>
            </div>`;
        return;
    }

    container.innerHTML = '';
    // Stack is LIFO (top of stack at top)
    for (let i = repo.rollbackStack.length - 1; i >= 0; i--) {
        const item = repo.rollbackStack[i];
        const el = document.createElement('div');
        el.className = 'stack-item';
        el.innerHTML = `
            <div><strong>Top ${i + 1}:</strong> ${item.description}</div>
            <small style="font-size:0.75rem;">Pushed at ${item.timestamp}</small>
        `;
        container.appendChild(el);
    }
}

// View 4: .minigit File Tree
function renderFileTree() {
    const container = document.getElementById('fileTreeContainer');
    const badge = document.getElementById('fsStatusBadge');
    if (!container) return;

    if (!repo.isInitialized) {
        if (badge) badge.textContent = 'Not Initialized';
        container.innerHTML = `
            <div class="empty-state">
                <i class="fa-solid fa-folder-closed"></i>
                <p>No <code>.minigit/</code> directory found.</p>
                <span>Run <code>minigit init</code> to generate storage structure.</span>
            </div>`;
        return;
    }

    if (badge) badge.textContent = 'Active (.minigit)';

    let objectCount = repo.commits.length;
    let headStr = repo.headCommit ? repo.headCommit.id : 'ref: refs/heads/main';

    container.innerHTML = `
        <div class="tree-item folder"><i class="fa-solid fa-folder-open"></i> .minigit/</div>
        <div class="tree-item folder" style="padding-left:16px;"><i class="fa-solid fa-folder-open"></i> objects/ (${objectCount} commit blobs)</div>
        <div class="tree-item file" style="padding-left:32px;"><i class="fa-solid fa-file"></i> index (staging area hash map)</div>
        <div class="tree-item file" style="padding-left:32px;"><i class="fa-solid fa-file-code"></i> HEAD -> [${headStr}]</div>
        <div class="tree-item folder" style="padding-left:16px;"><i class="fa-solid fa-folder"></i> refs/</div>
        <div class="tree-item file" style="padding-left:32px;"><i class="fa-solid fa-file"></i> refs/heads/main</div>
    `;
}

// --------------------------------------------------------------------------
// 4. HELPER UTILITIES & PRESETS
// --------------------------------------------------------------------------
function executeInSandbox(cmd) {
    const input = document.getElementById('cliInput');
    if (input) {
        input.value = cmd;
        document.getElementById('cliSendBtn').click();
    }
    // Scroll smoothly to simulator section
    const simSection = document.getElementById('simulator');
    if (simSection) {
        simSection.scrollIntoView({ behavior: 'smooth' });
    }
}

function copyText(text) {
    navigator.clipboard.writeText(text).then(() => {
        showToast("Command copied to clipboard!");
    }).catch(err => {
        console.error("Failed to copy", err);
    });
}

function showToast(msg) {
    const toast = document.createElement('div');
    toast.style.cssText = `
        position: fixed;
        bottom: 24px;
        right: 24px;
        background: var(--cyan);
        color: var(--text-dark);
        font-weight: 700;
        padding: 10px 20px;
        border-radius: 10px;
        box-shadow: var(--shadow-lg);
        z-index: 9999;
        font-size: 0.9rem;
        animation: popIn 0.2s cubic-bezier(0.175, 0.885, 0.32, 1.275);
    `;
    toast.textContent = msg;
    document.body.appendChild(toast);
    setTimeout(() => toast.remove(), 2500);
}

// Preset Workflows
function runPresetWorkflow(type) {
    resetSandbox();
    
    if (type === 'full') {
        executeCommandString('minigit init');
        setTimeout(() => executeCommandString('minigit add main.cpp'), 400);
        setTimeout(() => executeCommandString('minigit add utils.h'), 800);
        setTimeout(() => executeCommandString('minigit commit -m "Initial repository commit"'), 1200);
        setTimeout(() => executeCommandString('minigit add main.cpp'), 1600);
        setTimeout(() => executeCommandString('minigit commit -m "Add core data structure logic"'), 2000);
    } else if (type === 'init_add_commit') {
        executeCommandString('minigit init');
        setTimeout(() => executeCommandString('minigit add index.html'), 400);
        setTimeout(() => executeCommandString('minigit commit -m "Initial commit"'), 800);
    } else if (type === 'checkout_demo') {
        executeCommandString('minigit init');
        setTimeout(() => executeCommandString('minigit add file1.txt'), 300);
        setTimeout(() => executeCommandString('minigit commit -m "Commit 1"'), 600);
        setTimeout(() => executeCommandString('minigit add file2.txt'), 900);
        setTimeout(() => executeCommandString('minigit commit -m "Commit 2"'), 1200);
        setTimeout(() => executeCommandString('minigit checkout c1'), 1600);
    }
}

function resetSandbox() {
    repo.resetState();
    const termOutput = document.getElementById('terminalOutput');
    if (termOutput) termOutput.innerHTML = '';
    printTermLine("Repository reset. Ready for new operations.", 'warning');
    updateVisualizers();
}
