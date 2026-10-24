'use strict';

const EXAMPLES = [
  {name: 'main.astra', label: 'Welcome to Astra', source: `// Welcome to Astra Studio ✦
// A real language, compiled to bytecode in C++20.

fn fibonacci(n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print("✦ Welcome to Astra Studio");
print("──────────────────────────");

let i = 0;
while (i < 9) {
    print("fib(" + str(i) + ") = " + str(fibonacci(i)));
    i = i + 1;
}

print("All done. Build something incredible!");
`},
  {name:'primes.astra', label:'Prime number sieve', source:`// Find prime numbers using a primality test.
fn isPrime(n) {
    if (n < 2) { return false; }
    let divisor = 2;
    while (divisor * divisor <= n) {
        if (n % divisor == 0) { return false; }
        divisor = divisor + 1;
    }
    return true;
}

let n = 1;
while (n <= 60) {
    if (isPrime(n)) { print("Prime: " + str(n)); }
    n = n + 1;
}
`},
  {name:'recursion.astra', label:'Recursive factorial', source:`// Functions, recursion and assertions.
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

assert(factorial(5) == 120);
let i = 1;
while (i <= 10) {
    print(str(i) + "! = " + str(factorial(i)));
    i = i + 1;
}
`},
  {name:'simulation.astra', label:'Population simulation', source:`// A simple mathematical population model.
// No imports or external runtime required.

fn nextPopulation(population, growth) {
    return population + population * growth;
}

let population = 1500;
let year = 2026;
let growth = 0.075;

print("YEAR     POPULATION");
while (year <= 2040) {
    print(str(year) + "     " + str(population));
    population = nextPopulation(population, growth);
    year = year + 1;
}
`},
  {name:'logic.astra', label:'Logic & conditions', source:`// Astra has short-circuit boolean operators.
fn classify(value) {
    if (value < 0) { return "negative"; }
    if (value == 0) { return "zero"; }
    return "positive";
}

let x = -3;
while (x <= 3) {
    print(str(x) + " is " + classify(x));
    x = x + 1;
}

print("Operators: " + str(true || false));
print("Short-circuit: " + str(false && (1 / 0)));
`},
  {name:'errors.astra', label:'Error diagnostics', source:`// Try this example to inspect an error.
fn divide(a, b) {
    return a / b;
}

print("About to trigger a runtime error...");
print(divide(42, 0));
`}
];

const $ = id => document.getElementById(id);
const source = $('source');
const highlight = $('highlight');
const lines = $('lineNumbers');
const fileList = $('fileList');
const consoleContent = $('consoleContent');
const STORAGE = 'astra-studio-source-v1';
const FILE_STORAGE = 'astra-studio-file-v1';
let currentName = localStorage.getItem(FILE_STORAGE) || EXAMPLES[0].name;
let currentSource = localStorage.getItem(STORAGE) ?? EXAMPLES[0].source;
let savedSource = currentSource;
let currentExample = EXAMPLES.some(x => x.name === currentName) ? currentName : null;
let latestOutput = '';
let activeBusy = false;
let toastTimer;

function toast(message) {
  const el = $('toast');
  el.textContent = message;
  el.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), 2400);
}
function escapeHTML(s) {
  return s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
}
function syntax(text) {
  const matcher = /\/\/[^\n]*|"(?:\\.|[^"\\])*"|\b(?:let|fn|if|else|while|return|true|false|null)\b|\b(?:print|sqrt|assert|len|str)\b|\b\d+(?:\.\d+)?\b|(?:==|!=|<=|>=|&&|\|\||[+\-*\/%=<>!])/g;
  let last = 0, rendered = '', match;
  while ((match = matcher.exec(text)) !== null) {
    rendered += escapeHTML(text.slice(last, match.index));
    const token = match[0];
    const kind = token.startsWith('//') ? 'comment' : token.startsWith('"') ? 'string'
      : /^(let|fn|if|else|while|return|true|false|null)$/.test(token) ? 'keyword'
      : /^(print|sqrt|assert|len|str)$/.test(token) ? 'builtin'
      : /^\d/.test(token) ? 'number' : 'operator';
    rendered += `<span class="token-${kind}">${escapeHTML(token)}</span>`;
    last = matcher.lastIndex;
  }
  return rendered + escapeHTML(text.slice(last)) + (text.endsWith('\n') ? ' ' : '');
}
function editorRefresh() {
  highlight.innerHTML = syntax(source.value);
  const lineCount = source.value.split('\n').length;
  lines.textContent = Array.from({length:lineCount}, (_, i) => i+1).join('\n');
  $('lineCount').textContent = `${lineCount} line${lineCount === 1 ? '' : 's'}`;
  const position = source.selectionStart;
  const textBefore = source.value.slice(0,position);
  const line = textBefore.split('\n').length;
  const column = textBefore.length - textBefore.lastIndexOf('\n');
  $('cursorPos').textContent = `Ln ${line}, Col ${column}`;
  $('dirtyDot').classList.toggle('dirty', source.value !== savedSource);
  highlight.scrollTop = source.scrollTop;
  highlight.scrollLeft = source.scrollLeft;
  lines.scrollTop = source.scrollTop;
}
function setFile(name, code, example = null) {
  currentName = name;
  currentExample = example;
  source.value = code;
  savedSource = code;
  $('fileTabName').textContent = name;
  $('breadcrumbName').textContent = name;
  localStorage.setItem(FILE_STORAGE, name);
  localStorage.setItem(STORAGE, code);
  document.title = `${name} — Astra Studio`;
  rebuildList();
  editorRefresh();
  source.focus();
  $('compileStatus').textContent = 'Ready to compile';
}
function rebuildList() {
  fileList.replaceChildren();
  for (const sample of EXAMPLES) {
    const btn = document.createElement('button');
    btn.className = 'file-item' + (currentExample === sample.name ? ' active' : '');
    btn.title = sample.label;
    const ico = document.createElement('span'); ico.className = 'file-icon'; ico.textContent = '✳';
    const label = document.createElement('span'); label.className = 'file-label'; label.textContent = sample.name;
    const tail = document.createElement('span'); tail.className = 'file-right'; tail.textContent = 'AS';
    btn.append(ico,label,tail);
    btn.addEventListener('click',() => {
      if (source.value !== savedSource && !confirm('Discard unsaved editor changes and open an example?')) return;
      setFile(sample.name,sample.source,sample.name);
      toast(`Opened ${sample.name}`);
    });
    fileList.append(btn);
  }
}
function newFile() {
  if (source.value !== savedSource && !confirm('Discard unsaved editor changes?')) return;
  setFile('untitled.astra', '// Start creating something amazing.\n\n', null);
  toast('New playground ready');
}
function saveFile() {
  const blob = new Blob([source.value],{type:'text/plain;charset=utf-8'});
  const href = URL.createObjectURL(blob);
  const a = document.createElement('a');a.href = href;a.download = currentName.endsWith('.astra') ? currentName : currentName + '.astra';
  document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(href),1500);
  savedSource=source.value;editorRefresh();toast('File downloaded');
}
function openFile(file) {
  if (!file) return;
  if (file.size > 131072) return toast('File too large (128 KiB maximum)');
  const reader = new FileReader();
  reader.onload = () => {setFile(file.name,String(reader.result),null);toast('File opened');};
  reader.readAsText(file);
}
function formatCode() {
  const before = source.value;
  let level=0;
  let inBlockComment=false;
  const formatted = before.split('\n').map(line => {
    const trim=line.trim();
    if (!trim) return '';
    // This deliberately offers simple indentation, not a semantics-preserving parser formatter.
    if (trim.startsWith('}')) level=Math.max(0,level-1);
    const result='    '.repeat(level)+trim;
    if (/\{\s*(?:\/\/.*)?$/.test(trim) && !/^\/\//.test(trim)) ++level;
    inBlockComment = inBlockComment && false;
    return result;
  }).join('\n');
  source.value=formatted;
  editorRefresh();toast('Indentation normalized');
}
function selectView(name) {
  document.querySelectorAll('.rail-link').forEach(el => el.classList.toggle('selected',el.dataset.view===name));
  for (const view of ['explorer','guide','about']) $('panel-'+view).classList.toggle('hidden',view!==name);
  $('sidebar').classList.remove('collapsed');
}
function inspectPanel(panel) {
  document.querySelectorAll('.inspector-tab').forEach(el=>el.classList.toggle('active',el.dataset.inspector===panel));
  $('bytecodePanel').classList.toggle('hidden',panel!=='bytecode');
  $('referencePanel').classList.toggle('hidden',panel!=='help');
}
function showResult(data,ran) {
  const okay=data.ok;
  $('instructionMetric').textContent = data.instructions.toLocaleString();
  $('functionMetric').textContent = data.functions.toLocaleString();
  $('stepMetric').textContent = ran ? data.steps.toLocaleString() : '—';
  $('timeMetric').textContent = data.elapsedMs.toFixed(2) + 'ms';
  $('inspectorBadge').textContent = okay ? 'SUCCESS' : 'ERROR';
  $('inspectorBadge').className = 'inspector-count ' + (okay?'success':'fail');
  $('disassembly').textContent = data.disassembly || '// Compiler could not generate bytecode.';
  $('disassembly').classList.remove('hidden');
  const placeholder=document.querySelector('.placeholder-inspector');placeholder.classList.add('hidden');
  inspectPanel('bytecode');
  $('sidebarPipeline').classList.toggle('success',okay);
  $('sidebarPipeline').classList.toggle('error',!okay);
  $('compileStatus').textContent=okay?(ran?'Execution succeeded':'Compilation succeeded'):`Error on line ${data.errorLine || '?'}`;
  $('compileStatus').parentElement.classList.toggle('fail',!okay);
  const mode=ran?'EXECUTED':'ANALYZED';
  $('consoleState').textContent=okay?'SUCCESS':'ERROR';
  $('consoleState').className='console-state '+(okay?'success':'fail');
  $('lastRun').textContent=`${mode} · ${data.elapsedMs.toFixed(2)}ms`;
  const lines=[];
  if(data.output) lines.push(data.output.trimEnd());
  if(!okay) lines.push(`Error${data.errorLine ? ' at line '+data.errorLine : ''}: ${data.error}`);
  if(okay && !data.output) lines.push(ran?'Program finished. (No output)':'Compilation successful. No code was executed.');
  if (okay) lines.push(`\n✓ ${ran?'Finished':'Compiled'} in ${data.elapsedMs.toFixed(2)}ms · ${data.instructions} bytecode ops${ran?' · '+data.steps+' VM steps':''}`);
  latestOutput=lines.join('\n');
  consoleContent.replaceChildren();
  const pre=document.createElement('pre');pre.textContent=latestOutput;
  pre.className=okay?'console-success':'console-error';
  consoleContent.append(pre);
  consoleContent.scrollTop=consoleContent.scrollHeight;
  if(!okay && data.errorLine) {
    const textLines=source.value.split('\n');
    const offset=textLines.slice(0,data.errorLine-1).reduce((sum,line)=>sum+line.length+1,0);
    source.focus();source.setSelectionRange(offset,offset + (textLines[data.errorLine-1]?.length || 0));
    editorRefresh();
  }
}
async function compile(ran) {
  if(activeBusy) return;
  activeBusy=true;
  const label=ran?'Running…':'Analyzing…';
  $('runButton').disabled=true;$('analyzeButton').disabled=true;
  $('compileStatus').textContent=label;
  $('consoleState').textContent=ran?'RUNNING':'COMPILING';
  $('consoleState').className='console-state running';
  try {
    const response=await fetch(ran?'/api/run':'/api/analyze',{
      method:'POST',headers:{'Content-Type':'text/plain; charset=utf-8'},body:source.value
    });
    if(!response.ok) throw new Error(`HTTP ${response.status}: ${await response.text()}`);
    showResult(await response.json(),ran);
  } catch(err) {
    consoleContent.textContent='Could not contact the local Astra engine: '+err.message;
    $('consoleState').textContent='OFFLINE';$('consoleState').className='console-state fail';
    $('compileStatus').textContent='Connection to engine failed';
    toast('Runtime unavailable');
  } finally {
    activeBusy=false;$('runButton').disabled=false;$('analyzeButton').disabled=false;
  }
}

source.value=currentSource;
$('fileTabName').textContent=currentName;
$('breadcrumbName').textContent=currentName;
rebuildList();
editorRefresh();
source.addEventListener('input',()=>{
  localStorage.setItem(STORAGE,source.value);
  currentExample=null;
  rebuildList();
  editorRefresh();
});
source.addEventListener('scroll',editorRefresh);
source.addEventListener('click',editorRefresh);
source.addEventListener('keyup',editorRefresh);
source.addEventListener('keydown',event=>{
  if(event.key==='Tab') {
    event.preventDefault();
    const from=source.selectionStart,to=source.selectionEnd;
    source.setRangeText('    ',from,to,'end');
    source.dispatchEvent(new Event('input'));
  }
});
document.addEventListener('keydown',event=>{
  if((event.ctrlKey||event.metaKey)&&event.key==='Enter') {event.preventDefault();compile(true);}
  if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s') {event.preventDefault();saveFile();}
  if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='o') {event.preventDefault();$('fileInput').click();}
});
$('newButton').addEventListener('click',newFile);
$('sidebarNew').addEventListener('click',newFile);
$('openButton').addEventListener('click',()=>$('fileInput').click());
$('fileInput').addEventListener('change',event=>{openFile(event.target.files[0]);event.target.value='';});
$('saveButton').addEventListener('click',saveFile);
$('formatButton').addEventListener('click',formatCode);
$('runButton').addEventListener('click',()=>compile(true));
$('analyzeButton').addEventListener('click',()=>compile(false));
$('copyOutput').addEventListener('click',async()=>{try{await navigator.clipboard.writeText(latestOutput);toast('Output copied');}catch{toast('Copy failed — browser denied clipboard access');}});
$('clearOutput').addEventListener('click',()=>{latestOutput='';consoleContent.textContent='Console cleared.';$('consoleState').textContent='CLEARED';$('consoleState').className='console-state';});
$('showSidebar').addEventListener('click',()=>$('sidebar').classList.toggle('collapsed'));
$('collapseSidebar').addEventListener('click',()=>$('sidebar').classList.add('collapsed'));
$('openGuide').addEventListener('click',()=>selectView('guide'));
document.querySelectorAll('.rail-link').forEach(btn=>btn.addEventListener('click',()=>selectView(btn.dataset.view)));
document.querySelectorAll('.inspector-tab').forEach(btn=>btn.addEventListener('click',()=>inspectPanel(btn.dataset.inspector)));
