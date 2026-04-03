/**
 * C-Prime Releases Provider — releases.ts
 * =========================================
 * Fetches and displays GitHub release notes in a styled webview panel.
 * Shows the latest 5 releases with download links and changelog.
 */

import * as vscode from 'vscode';
import * as https  from 'https';

const RELEASES_URL = 'https://api.github.com/repos/cprime-lang/cprime/releases';
const GITHUB_URL   = 'https://github.com/cprime-lang/cprime/releases';

// ─── Fetch helper (no axios dependency) ───────────────────────────────────────

function fetchJson(url: string): Promise<any> {
    return new Promise((resolve, reject) => {
        const opts = {
            headers: {
                'User-Agent': 'cprime-vscode-extension/0.1.0',
                'Accept':     'application/vnd.github.v3+json',
            }
        };
        https.get(url, opts, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try   { resolve(JSON.parse(data)); }
                catch { reject(new Error('JSON parse error')); }
            });
        }).on('error', reject);
    });
}

// ─── HTML Builder ─────────────────────────────────────────────────────────────

interface Release {
    tag_name:     string;
    name:         string;
    published_at: string;
    body:         string;
    html_url:     string;
    assets:       { name: string; browser_download_url: string; size: number }[];
    prerelease:   boolean;
    draft:        boolean;
}

function formatBody(body: string): string {
    if (!body) return '<p>No release notes.</p>';
    // Convert markdown headings and bullets to HTML
    return body
        .replace(/^### (.+)$/gm,  '<h4>$1</h4>')
        .replace(/^## (.+)$/gm,   '<h3>$1</h3>')
        .replace(/^# (.+)$/gm,    '<h2>$1</h2>')
        .replace(/^\* (.+)$/gm,   '<li>$1</li>')
        .replace(/^- (.+)$/gm,    '<li>$1</li>')
        .replace(/`([^`]+)`/g,    '<code>$1</code>')
        .replace(/\*\*(.+?)\*\*/g,'<strong>$1</strong>')
        .replace(/\n\n/g,         '</p><p>')
        .replace(/<li>/g,         '</p><ul><li>')
        .replace(/<\/li>\n/g,     '</li></ul><p>');
}

function formatSize(bytes: number): string {
    if (bytes > 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
    if (bytes > 1024)         return `${(bytes / 1024).toFixed(0)} KB`;
    return `${bytes} B`;
}

function buildReleasesHtml(releases: Release[]): string {
    const items = releases
        .filter(r => !r.draft)
        .slice(0, 6)
        .map(r => {
            const date  = new Date(r.published_at).toLocaleDateString('en-US', {
                year: 'numeric', month: 'long', day: 'numeric'
            });
            const badge = r.prerelease
                ? '<span class="badge pre">pre-release</span>'
                : '<span class="badge stable">stable</span>';

            const assets = r.assets.length
                ? `<div class="assets">
                    ${r.assets.map(a => `
                        <a class="asset-link" href="${a.browser_download_url}">
                            <span class="asset-icon">${a.name.endsWith('.deb') ? '📦' : a.name.endsWith('.vslnx') ? '🔌' : '📄'}</span>
                            <span>${a.name}</span>
                            <span class="asset-size">${formatSize(a.size)}</span>
                        </a>`).join('')}
                   </div>`
                : '';

            return `
<div class="release">
    <div class="release-header">
        <div class="release-title">
            <span class="tag">${r.tag_name}</span>
            <span class="name">${r.name || r.tag_name}</span>
            ${badge}
        </div>
        <span class="date">📅 ${date}</span>
    </div>
    <div class="release-body"><p>${formatBody(r.body || '')}</p></div>
    ${assets}
    <a class="gh-link" href="${r.html_url}">View on GitHub →</a>
</div>`;
        }).join('');

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>C-Prime Releases</title>
<style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
        font-family: 'Segoe UI', system-ui, sans-serif;
        background: #0d1117;
        color: #e6edf3;
        padding: 28px 32px;
        line-height: 1.6;
    }
    .header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 28px;
        padding-bottom: 16px;
        border-bottom: 1px solid #30363d;
    }
    .header-title { font-size: 22px; color: #58a6ff; font-weight: 700; }
    .header-sub   { color: #6e7681; font-size: 13px; margin-top: 2px; }
    .gh-all-link  { color: #58a6ff; font-size: 13px; text-decoration: none; }
    .gh-all-link:hover { text-decoration: underline; }
    .release {
        background: #161b22;
        border: 1px solid #30363d;
        border-radius: 10px;
        padding: 20px 24px;
        margin-bottom: 18px;
    }
    .release:hover { border-color: #58a6ff44; }
    .release-header {
        display: flex;
        align-items: flex-start;
        justify-content: space-between;
        margin-bottom: 12px;
    }
    .release-title { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
    .tag  {
        background: #1f6feb;
        color: #fff;
        padding: 2px 10px;
        border-radius: 12px;
        font-size: 12px;
        font-weight: 700;
        font-family: monospace;
    }
    .name { font-size: 16px; font-weight: 600; color: #e6edf3; }
    .badge {
        padding: 1px 8px;
        border-radius: 10px;
        font-size: 11px;
        font-weight: 600;
    }
    .badge.stable { background: #1a7f37; color: #7ee787; }
    .badge.pre    { background: #5a3d00; color: #e3b341; }
    .date { color: #6e7681; font-size: 12px; white-space: nowrap; }
    .release-body { 
        color: #8b949e; 
        font-size: 13px; 
        margin-bottom: 16px;
        max-height: 200px;
        overflow: hidden;
        position: relative;
    }
    .release-body::after {
        content: '';
        position: absolute;
        bottom: 0; left: 0; right: 0;
        height: 40px;
        background: linear-gradient(transparent, #161b22);
    }
    .release-body h2,h3,h4 { color: #79c0ff; margin: 10px 0 6px; font-size: 14px; }
    .release-body code { background: #1c2128; padding: 1px 5px; border-radius: 3px; color: #79c0ff; }
    .release-body li { margin-left: 18px; }
    .assets { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 14px; }
    .asset-link {
        display: inline-flex;
        align-items: center;
        gap: 6px;
        padding: 5px 12px;
        background: #21262d;
        border: 1px solid #30363d;
        border-radius: 6px;
        color: #79c0ff;
        text-decoration: none;
        font-size: 12px;
        transition: all 0.15s;
    }
    .asset-link:hover { background: #30363d; border-color: #58a6ff; }
    .asset-size { color: #6e7681; font-size: 11px; }
    .gh-link { color: #58a6ff; font-size: 13px; text-decoration: none; }
    .gh-link:hover { text-decoration: underline; }
    .loading, .error {
        text-align: center;
        padding: 60px 20px;
        color: #6e7681;
    }
    .error { color: #f85149; }
</style>
</head>
<body>
<div class="header">
    <div>
        <div class="header-title">🏷️ C-Prime Releases</div>
        <div class="header-sub">Latest compiler, tools, and VS Code extension releases</div>
    </div>
    <a class="gh-all-link" href="${GITHUB_URL}">All releases on GitHub →</a>
</div>
${items || '<div class="error">No releases found. The project may not have published a release yet.</div>'}
</body>
</html>`;
}

function buildLoadingHtml(): string {
    return `<!DOCTYPE html><html><head><style>
        body { background:#0d1117; color:#6e7681; font-family:system-ui;
               display:flex; align-items:center; justify-content:center; height:100vh; margin:0; }
        .spinner { text-align:center; }
        .dot { display:inline-block; width:8px; height:8px; border-radius:50%;
               background:#58a6ff; margin:0 3px; animation:bounce 1.2s ease-in-out infinite; }
        .dot:nth-child(2) { animation-delay:0.2s; }
        .dot:nth-child(3) { animation-delay:0.4s; }
        @keyframes bounce { 0%,80%,100%{transform:scale(0.6)} 40%{transform:scale(1)} }
        p { margin-top:16px; font-size:14px; }
    </style></head><body>
    <div class="spinner">
        <div class="dot"></div><div class="dot"></div><div class="dot"></div>
        <p>Loading releases…</p>
    </div></body></html>`;
}

function buildErrorHtml(msg: string): string {
    return `<!DOCTYPE html><html><head><style>
        body { background:#0d1117; color:#f85149; font-family:system-ui;
               padding:40px; }
        h2 { margin-bottom:12px; }
        p  { color:#8b949e; font-size:14px; }
        a  { color:#58a6ff; }
    </style></head><body>
    <h2>⚠ Could not load releases</h2>
    <p>${msg}</p>
    <p>Check your internet connection, or <a href="${GITHUB_URL}">view releases on GitHub</a>.</p>
    </body></html>`;
}

// ─── Public API ───────────────────────────────────────────────────────────────

export async function createReleasesPanel(context: vscode.ExtensionContext): Promise<void> {
    const panel = vscode.window.createWebviewPanel(
        'cprimeReleases',
        'C-Prime Releases',
        vscode.ViewColumn.One,
        { enableScripts: true, retainContextWhenHidden: true }
    );

    // Show loading state immediately
    panel.webview.html = buildLoadingHtml();

    // Handle links opened from the webview
    panel.webview.onDidReceiveMessage(msg => {
        if (msg.command === 'open' && msg.url) {
            vscode.env.openExternal(vscode.Uri.parse(msg.url));
        }
    }, undefined, context.subscriptions);

    try {
        const releases: Release[] = await fetchJson(RELEASES_URL);
        panel.webview.html = buildReleasesHtml(releases);
    } catch (err: any) {
        panel.webview.html = buildErrorHtml(err?.message || 'Unknown error');
    }
}
