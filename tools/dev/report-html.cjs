// Build a self-contained HTML page from docs/porting/REPORT.md (the owner review of 2026-10-08): a sticky table of contents (sections and features),
// a filter box over the contents, styled tables.
//   node tools/dev/report-html.cjs docs/porting/REPORT.md docs/porting/REPORT.html
// Needs the frontend clone's node_modules (marked); bin/publish-preview serves the page at /report/.
const fs = require('fs');
const { marked } = require('/home/ion/WorkUC2/ufrontend/node_modules/marked');
const [src, out] = process.argv.slice(2);
const md = fs.readFileSync(src, 'utf8');

const slugs = new Map();
function slug(text) {
  let s = text.toLowerCase().replace(/<[^>]+>/g, '').replace(/&#?\w+;/g, '').replace(/[`*_]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 's';
  const n = slugs.get(s) || 0; slugs.set(s, n + 1);
  return n ? `${s}-${n}` : s;
}
const toc = [];
const renderer = new marked.Renderer();
renderer.heading = function (token) {
  const text = this.parser.parseInline(token.tokens);
  const plain = text.replace(/<[^>]+>/g, '');
  const id = slug(plain);
  if (token.depth <= 3) toc.push({ depth: token.depth, id, text: plain });
  return `<h${token.depth} id="${id}"><a class="anchor" href="#${id}">#</a>${text}</h${token.depth}>\n`;
};
renderer.table = function (token) {
  const html = marked.Renderer.prototype.table.call(this, token);
  return `<div class="tablewrap">${html}</div>`;
};
marked.use({ renderer, gfm: true });
const body = marked.parse(md);
const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
const tocHtml = toc.map(t => `<li class="d${t.depth}"><a href="#${t.id}">${t.text}</a></li>`).join('\n');
const stamp = new Date().toISOString().slice(0, 16).replace('T', ' ');

const html = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>UC2 Engine Changes</title>
<style>
:root{--bg:#fbfaf7;--fg:#1d1d1b;--muted:#6b6a64;--line:#e3e0d8;--side:#f2f0ea;--accent:#8a3b12;--code:#efece4;--th:#ece8de}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#17171a;--fg:#e6e4de;--muted:#9a978f;--line:#2e2e33;--side:#1e1e22;--accent:#e39a6a;--code:#24242a;--th:#26262c}}
:root[data-theme="dark"]{--bg:#17171a;--fg:#e6e4de;--muted:#9a978f;--line:#2e2e33;--side:#1e1e22;--accent:#e39a6a;--code:#24242a;--th:#26262c}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--fg)}
body{font:15px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif;display:grid;grid-template-columns:320px minmax(0,1fr)}
nav{position:sticky;top:0;height:100vh;overflow:auto;background:var(--side);border-right:1px solid var(--line);padding:16px 12px}
nav h2{font-size:14px;margin:0 0 8px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
nav input{width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);margin-bottom:10px}
nav ul{list-style:none;margin:0;padding:0}nav li{margin:1px 0}nav a{color:var(--fg);text-decoration:none;display:block;padding:2px 4px;border-radius:4px}
nav a:hover{background:var(--line)}nav li.d1 a{font-weight:700;margin-top:10px}nav li.d2 a{padding-left:12px;font-weight:600;font-size:14px}nav li.d3 a{padding-left:24px;font-size:13px;color:var(--muted)}
main{padding:24px 40px 80px;max-width:1150px}
h1{font-size:28px;border-bottom:2px solid var(--line);padding-bottom:6px;margin-top:48px}h2{font-size:22px;margin-top:36px}h3{font-size:18px;margin-top:30px;color:var(--accent)}
h1,h2,h3,h4{position:relative;scroll-margin-top:12px}.anchor{position:absolute;left:-18px;color:var(--muted);text-decoration:none;opacity:0}h1:hover .anchor,h2:hover .anchor,h3:hover .anchor{opacity:1}
a{color:var(--accent)}code{font:13px/1.4 ui-monospace,"SFMono-Regular",Menlo,Consolas,monospace;background:var(--code);padding:1px 4px;border-radius:4px;overflow-wrap:anywhere}
pre{background:var(--code);padding:10px 12px;border-radius:6px;overflow:auto}pre code{padding:0;background:none}
.tablewrap{overflow-x:auto;margin:12px 0}table{border-collapse:collapse;font-size:13.5px;min-width:60%}th,td{border:1px solid var(--line);padding:5px 8px;vertical-align:top;text-align:left}th{background:var(--th)}
blockquote{margin:12px 0;padding:4px 14px;border-left:3px solid var(--accent);color:var(--muted)}
.stamp{color:var(--muted);font-size:13px}#top{position:fixed;right:16px;bottom:16px;background:var(--side);border:1px solid var(--line);border-radius:6px;padding:6px 10px;color:var(--fg);text-decoration:none}
@media (max-width:860px){body{display:block}nav{position:static;height:auto;max-height:45vh;border-right:0;border-bottom:1px solid var(--line)}main{padding:16px}h1,h2,h3{word-break:break-word}.anchor{display:none}}
</style></head><body>
<nav><h2>Contents</h2><input id="f" type="search" placeholder="Filter contents..." aria-label="Filter contents"><ul id="toc">
${tocHtml}
</ul></nav>
<main><p class="stamp">Built ${stamp} UTC from <code>docs/porting/REPORT.md</code> (engine-uc2). Local only.</p>
${body}
</main><a id="top" href="#">Top</a>
<script>
const f=document.getElementById('f'),items=[...document.querySelectorAll('#toc li')];
f.addEventListener('input',()=>{const q=f.value.toLowerCase();items.forEach(li=>{li.style.display=!q||li.textContent.toLowerCase().includes(q)?'':'none'})});
</script></body></html>
`;
fs.writeFileSync(out, html);
console.log(`wrote ${out}: ${html.length} bytes, ${toc.length} headings`);
