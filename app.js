/*
 * ASU FOE · Smart Staff Experience — dashboard logic.
 * Plain JavaScript, no build step. Data comes from js/data.js (window.DEMO_DATA).
 */
(function () {
  'use strict';

  const D = window.DEMO_DATA;
  // Today on this device, as YYYY-MM-DD in local time.
  const TODAY = (() => {
    const n = new Date();
    return `${n.getFullYear()}-${String(n.getMonth() + 1).padStart(2, '0')}-${String(n.getDate()).padStart(2, '0')}`;
  })();
  let ME = null; // whoever is signed in
  const STORE_KEY = 'asu-foe-smart-staff:data:v4'; // v4: real dates instead of a pinned demo day
  const THEME_KEY = 'asu-foe-smart-staff:theme';
  const ACCOUNTS_KEY = 'asu-foe-smart-staff:accounts:v1';
  const SESSION_KEY = 'asu-foe-smart-staff:session';

  /* ------------------------------------------------------------------ helpers */

  const $ = (sel, root) => (root || document).querySelector(sel);
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const icon = (name, cls) => `<i data-lucide="${name}"${cls ? ` class="${cls}"` : ''}></i>`;
  const plural = (n, one, many) => `${n} ${n === 1 ? one : (many || one + 's')}`;

  const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const MONTHS_LONG = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  const DOW = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DOW_LONG = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];

  const toDate = (s) => { const [y, m, d] = s.split('-').map(Number); return new Date(Date.UTC(y, m - 1, d)); };
  const isoOf = (dt) => dt.toISOString().slice(0, 10);
  const addDays = (s, n) => { const d = toDate(s); d.setUTCDate(d.getUTCDate() + n); return isoOf(d); };
  const daysBetween = (a, b) => Math.round((toDate(b) - toDate(a)) / 864e5);
  // The faculty week runs Saturday to Friday.
  const weekStartOf = (s) => addDays(s, -((toDate(s).getUTCDay() + 1) % 7));
  const dowOf = (s) => toDate(s).getUTCDay();
  const fmtDate = (s) => { const d = toDate(s); return `${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ${d.getUTCFullYear()}`; };
  const fmtShort = (s) => { const d = toDate(s); return `${DOW[d.getUTCDay()]}, ${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]}`; };
  const fmtLong = (s) => { const d = toDate(s); return `${DOW_LONG[d.getUTCDay()]}, ${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ${d.getUTCFullYear()}`; };
  const toMin = (hhmm) => { const [h, m] = hhmm.split(':').map(Number); return h * 60 + m; };
  const pad = (n) => String(n).padStart(2, '0');
  const fmtClock = (min) => { const h = Math.floor(min / 60); return `${pad(((h + 11) % 12) + 1)}:${pad(min % 60)} ${h < 12 ? 'AM' : 'PM'}`; };
  const fmt24 = (min) => `${pad(Math.floor(min / 60))}:${pad(min % 60)}`;
  const nowMin = () => { const n = new Date(); return n.getHours() * 60 + n.getMinutes(); };
  const nowLabel = () => fmtClock(nowMin());
  const greeting = () => { const m = nowMin(); return m < 12 * 60 ? 'Good Morning' : m < 17 * 60 ? 'Good Afternoon' : 'Good Evening'; };
  function relDue(s) {
    const n = daysBetween(TODAY, s);
    if (n === 0) return 'Due today';
    if (n === 1) return 'Due tomorrow';
    if (n === -1) return 'Overdue · 1 day';
    if (n < 0) return `Overdue · ${-n} days`;
    return `Due ${fmtShort(s)}`;
  }

  /* ------------------------------------------------------------------ people */

  const HUES = [212, 330, 24, 280, 152, 188, 12, 95, 262, 45, 200, 350];
  const STAFF = {};
  D.staff.forEach((s, i) => { STAFF[s.id] = Object.assign({ hue: HUES[i % HUES.length] }, s); });
  // The department manager assigns work but is not a named person in the team list.
  STAFF.manager = { id: 'manager', name: 'Department Manager', role: 'Manager', dept: '', hue: 200 };
  const DEPARTMENTS = D.departments;
  const hueFor = (id) => Array.from(id).reduce((h, c) => (h * 31 + c.charCodeAt(0)) % 360, 7);
  function addStaff(s) {
    if (STAFF[s.id]) return;
    STAFF[s.id] = Object.assign({ hue: hueFor(s.id) }, s);
    D.staff.push(s);
  }
  // Someone the manager sees in the shared database: known by name, but not an account on this device.
  function knowPerson(id, account) {
    const p = { id, name: String(account.name || 'Someone'), role: String(account.role || ''), dept: String(account.dept || '') };
    STAFF[id] = Object.assign({ hue: hueFor(id) }, STAFF[id] || {}, p);
  }
  const person = (id) => STAFF[id] || { id, name: 'Unknown', role: '', dept: '', hue: 215 };
  const firstName = (id) => person(id).name.replace(/^Dr\.\s*/, '').split(' ')[0];
  const initials = (name) => name.replace(/^Dr\.\s*/, '').split(/\s+/).map((w) => w[0]).slice(0, 2).join('').toUpperCase();
  function avatar(id, size) {
    const p = person(id);
    return `<span class="av${size ? ' ' + size : ''}" style="--h:${p.hue}" title="${esc(p.name)}" aria-hidden="true">${esc(initials(p.name))}</span>`;
  }

  /* ------------------------------------------------------------------ state */

  function freshState() {
    return { tasks: clone(D.tasks), requests: clone(D.requests), events: clone(D.events), notifications: clone(D.notifications), decisions: [], seq: 1, seeded: false };
  }
  function loadState() {
    try {
      const raw = localStorage.getItem(STORE_KEY);
      if (raw) {
        const s = JSON.parse(raw);
        if (s && Array.isArray(s.tasks) && Array.isArray(s.requests)) return s;
      }
    } catch (e) { /* storage blocked: fall back to the bundled demo data */ }
    return freshState();
  }
  function save() {
    try { localStorage.setItem(STORE_KEY, JSON.stringify(state)); } catch (e) { /* changes stay in memory only */ }
  }
  let state = loadState();
  // Example events (ids e1, e2 …) always follow data.js, so edits there reach everyone; meetings people added stay.
  state.events = state.events.filter((e) => !/^e\d+$/.test(e.id)).concat(clone(D.events));
  const newId = (prefix) => `${prefix}${Date.now().toString(36)}${(state.seq++).toString(36)}`;

  // Sensor readings are simulated and start fresh on every load.
  const campus = {
    occupied: new Set(D.campus.occupied),
    gate: Object.assign({}, D.campus.gate),
    env: Object.assign({}, D.campus.environment),
    occupancy: clone(D.campus.occupancy),
    accessLog: clone(D.campus.accessLog),
    live: true,
    slot: D.campus.mySlot
  };
  const TOTAL_SLOTS = D.campus.parkingZones.reduce((n, z) => n + z.slots, 0);

  const ui = {
    view: 'my-day',
    selected: 't1',
    drawer: false,
    taskScope: 'mine',
    taskFilter: 'all',
    reqFilter: 'all',
    search: '',
    week: 0,
    calDay: TODAY,
    sheet: false,
    modal: null,
    refocus: null,
    fileFor: null,
    confirmReset: false,
    authTab: 'signin',
    actFilter: 'all',
    actPerson: null,
    actConfirm: null,
    theme: 'system',
    prefs: { email: true, digest: true, parking: true }
  };

  /* ------------------------------------------------------------------ lookups */

  const STATUS = {
    'not-started': { label: 'Not Started', cls: 'ns', short: 'To Do' },
    'in-progress': { label: 'In Progress', cls: 'ip', short: 'In Progress' },
    awaiting: { label: 'Awaiting Approval', cls: 'aw', short: 'Awaiting' },
    completed: { label: 'Completed', cls: 'ok', short: 'Completed' }
  };
  const STATUS_KEYS = Object.keys(STATUS);
  const PRIORITY = { high: 'High', medium: 'Medium', low: 'Low' };
  const REQ_STATUS = {
    'in-progress': { label: 'In Progress', cls: 'ip' },
    awaiting: { label: 'Awaiting Approval', cls: 'aw' },
    approved: { label: 'Approved', cls: 'ok' },
    completed: { label: 'Completed', cls: 'ok' },
    rejected: { label: 'Rejected', cls: 'bad' }
  };
  const REQ_CAT = {
    maintenance: { label: 'Maintenance', icon: 'wrench', tone: 'aw' },
    electrical: { label: 'Electrical', icon: 'lightbulb', tone: 'warn' },
    equipment: { label: 'Equipment', icon: 'projector', tone: 'ok' },
    furniture: { label: 'Furniture', icon: 'armchair', tone: 'ip' },
    it: { label: 'IT & Network', icon: 'wifi', tone: 'violet' },
    cleaning: { label: 'Cleaning', icon: 'sparkles', tone: 'teal' }
  };

  const findTask = (id) => state.tasks.find((t) => t.id === id);
  const isOpen = (t) => t.status !== 'completed';
  const isOverdue = (t) => isOpen(t) && t.due < TODAY;
  // Staff see only tasks they own; the manager also sees tasks they handed to others.
  const isMine = (t) => t.owner === ME || (isManager() && t.by === ME);
  const myTasks = () => state.tasks.filter(isMine);
  const byDue = (a, b) => a.due.localeCompare(b.due);
  function progress(t) {
    if (t.status === 'completed') return 100;
    if (!t.subtasks.length) return 0;
    return Math.round((100 * t.subtasks.filter((s) => s.done).length) / t.subtasks.length);
  }
  // A day's events: the weekly timetable's classes for that weekday plus any one-off events.
  function eventsOn(day) {
    const dow = dowOf(day);
    const classes = (D.weeklySchedule || []).filter((c) => c.day === dow).map((c, i) => ({
      id: `w${dow}-${i}-${day}`, date: day, start: c.start, end: c.end, title: c.title, place: c.code, kind: c.kind, weekly: true
    }));
    return classes.concat(state.events.filter((e) => e.date === day)).sort((a, b) => a.start.localeCompare(b.start));
  }
  const breakOn = (day) => (D.breakDays || []).find((b) => b.day === dowOf(day));
  // Staff see only their own requests, and no approval requests meant for the manager.
  const myRequests = () => (isManager() ? state.requests : state.requests.filter((r) => r.by === ME));
  const myNotes = () => (isManager() ? state.notifications : state.notifications.filter((n) => n.view !== 'approvals'));
  function pendingApprovals() {
    return state.tasks.filter((t) => t.status === 'awaiting').map((t) => ({ kind: 'task', item: t }))
      .concat(state.requests.filter((r) => r.status === 'awaiting').map((r) => ({ kind: 'request', item: r })));
  }
  const wide = () => window.matchMedia('(min-width: 1360px)').matches;

  /* ------------------------------------------------------------------ small components */

  const statusPill = (s) => `<span class="pill ${STATUS[s].cls}">${STATUS[s].label}</span>`;
  const prioPill = (p) => `<span class="prio ${p}">${PRIORITY[p]}</span>`;
  const reqPill = (s) => `<span class="pill ${REQ_STATUS[s].cls}">${REQ_STATUS[s].label}</span>`;

  function progBar(t) {
    const p = progress(t);
    return `<div class="prog"><span class="prog-n">${p}%</span><span class="bar" role="progressbar" aria-valuenow="${p}" aria-valuemin="0" aria-valuemax="100" aria-label="Progress"><i style="width:${p}%"${p === 100 ? ' class="ok"' : ''}></i></span></div>`;
  }

  function kpi(o) {
    const tag = o.action ? 'button' : 'div';
    const attrs = o.action ? ` data-action="${o.action}"${o.data || ''}` : '';
    return `<${tag} class="kpi"${attrs}><span class="kpi-ico t-${o.tone}">${icon(o.icon)}</span><span class="kpi-txt"><b class="kpi-n">${o.n}</b><span class="kpi-l">${esc(o.label)}</span></span></${tag}>`;
  }

  // Ring chart: segments carry a 2px surface gap; hover shows the value.
  function donut(segs, size, stroke, centerN, centerL) {
    const total = segs.reduce((n, s) => n + s.value, 0) || 1;
    const r = (size - stroke) / 2;
    const c = 2 * Math.PI * r;
    const mid = size / 2;
    const gap = segs.filter((s) => s.value > 0).length > 1 ? 2.5 : 0;
    let acc = 0;
    const rings = segs.map((s) => {
      if (!s.value) return '';
      const len = (s.value / total) * c;
      const dash = Math.max(len - gap, 0.5);
      const out = `<circle class="seg" cx="${mid}" cy="${mid}" r="${r}" fill="none" style="stroke:${s.color}" stroke-width="${stroke}" stroke-dasharray="${dash.toFixed(2)} ${(c - dash).toFixed(2)}" stroke-dashoffset="${(-acc).toFixed(2)}" transform="rotate(-90 ${mid} ${mid})"><title>${esc(s.label)}: ${s.value} (${Math.round((100 * s.value) / total)}%)</title></circle>`;
      acc += len;
      return out;
    }).join('');
    const label = segs.map((s) => `${s.label} ${s.value}`).join(', ');
    return `<div class="donut" style="width:${size}px;height:${size}px"><svg width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" role="img" aria-label="${esc(label)}"><circle cx="${mid}" cy="${mid}" r="${r}" fill="none" style="stroke:var(--surface-3)" stroke-width="${stroke}"></circle>${rings}</svg><div class="donut-center"><b>${centerN}</b><small>${esc(centerL)}</small></div></div>`;
  }

  function barChart(items) {
    const max = Math.max(1, ...items.map((i) => i.value));
    const step = max <= 5 ? 1 : max <= 10 ? 2 : max <= 25 ? 5 : 10;
    const top = Math.ceil(max / step) * step;
    const ticks = [];
    for (let v = 0; v <= top; v += step) ticks.push(v);
    const label = items.map((i) => `${i.label} ${i.value}`).join(', ');
    return `<div class="bars" role="img" aria-label="${esc(label)}">
      <div class="bars-y">${ticks.map((v) => `<span style="bottom:${(v / top) * 100}%">${v}</span>`).join('')}</div>
      <div class="bars-plot">
        <div class="bars-grid">${ticks.slice(1).map((v) => `<i style="bottom:${(v / top) * 100}%"></i>`).join('')}</div>
        ${items.map((i) => `<div class="bar-col" title="${esc(i.label)}: ${i.value}"><span class="v">${i.value}</span><span class="b" style="height:${(i.value / top) * 100}%;background:${i.color}"></span></div>`).join('')}
      </div>
      <div class="bars-x">${items.map((i) => `<span>${esc(i.label)}</span>`).join('')}</div>
    </div>`;
  }

  /* ------------------------------------------------------------------ task table + details */

  function taskTable(list, emptyText) {
    if (!list.length) return `<div class="empty">${icon('inbox')}<p>${esc(emptyText || 'No tasks match.')}</p></div>`;
    return `<div class="ttable" role="table" aria-label="Tasks">
      <div class="trow thead" role="row"><span></span><span>Task</span><span>Priority</span><span class="c-owner-h">Owner</span><span>Deadline</span><span>Status</span><span>Progress</span></div>
      ${list.map(taskRow).join('')}
    </div>`;
  }

  function taskRow(t) {
    const done = t.status === 'completed';
    const cls = `trow${t.id === ui.selected ? ' is-sel' : ''}${done ? ' is-done' : ''}`;
    return `<div class="${cls}" role="row" data-action="select-task" data-id="${t.id}">
      <button class="chk${done ? ' on' : ''}" data-action="toggle-done" data-id="${t.id}" aria-label="${done ? 'Reopen' : 'Mark as done'}: ${esc(t.title)}" aria-pressed="${done}">${icon('check')}</button>
      <button class="c-title" data-action="select-task" data-id="${t.id}">${esc(t.title)}</button>
      <span class="c-pri">${prioPill(t.priority)}</span>
      <span class="c-owner">${avatar(t.owner)}<span class="owner-name">${esc(person(t.owner).name)}</span></span>
      <span class="c-due${isOverdue(t) ? ' overdue' : ''}"><span class="due-abs">${fmtDate(t.due)}</span><span class="due-rel">${done ? 'Done' : relDue(t.due)}</span></span>
      <span class="c-status">${statusPill(t.status)}</span>
      <span class="c-prog">${progBar(t)}</span>
    </div>`;
  }

  function detailWrap() {
    const t = findTask(ui.selected);
    return `<div class="detail-wrap" id="detail-wrap">${t ? detailPanel(t) : detailEmpty()}</div>`;
  }

  function detailEmpty() {
    return `<section class="card detail-empty">${icon('list-todo')}<b>No task selected</b><span>Choose a task to see its details, subtasks and comments.</span></section>`;
  }

  function fileRow(f) {
    const kind = f.kind || 'other';
    const ic = kind === 'sheet' ? 'file-spreadsheet' : kind === 'doc' ? 'file-text' : 'file';
    return `<div class="file"><span class="file-ico fk-${kind}">${icon(ic)}</span><div><b>${esc(f.name)}</b><small>${esc(f.size)}</small></div></div>`;
  }

  const commentRow = (c) => `<div class="comment">${avatar(c.by, 'av-sm')}<div><b>${esc(person(c.by).name)}</b><small>${esc(c.at)}</small><p>${esc(c.text)}</p></div></div>`;

  function detailPanel(t) {
    const done = t.subtasks.filter((s) => s.done).length;
    const p = progress(t);
    const closed = t.status === 'completed';
    const staffOpts = D.staff.map((s) => `<option value="${s.id}"${s.id === t.owner ? ' selected' : ''}>${esc(s.name)}</option>`).join('');
    const prioOpts = Object.keys(PRIORITY).map((k) => `<option value="${k}"${k === t.priority ? ' selected' : ''}>${PRIORITY[k]}</option>`).join('');
    const statusOpts = STATUS_KEYS.map((k) => `<option value="${k}"${k === t.status ? ' selected' : ''}>${STATUS[k].label}</option>`).join('');
    const subs = t.subtasks.map((s, i) => `<li><button class="chk sm${s.done ? ' on' : ''}" data-action="toggle-sub" data-id="${t.id}" data-i="${i}" aria-pressed="${s.done}" aria-label="${s.done ? 'Mark not done' : 'Mark done'}: ${esc(s.t)}">${icon('check')}</button><span class="${s.done ? 'struck' : ''}">${esc(s.t)}</span></li>`).join('');
    const log = t.log.slice(0, 6).map((l) => `<li>${avatar(l.by, 'av-sm')}<div><b>${esc(person(l.by).name)}</b><br><span>${esc(l.text)}</span></div><time>${esc(l.at)}</time></li>`).join('');

    return `
      <section class="card detail" aria-label="Task details">
        <div class="detail-top"><span class="eyebrow">Task Details</span><button class="icon-btn sm" data-action="close-detail" aria-label="Close task details">${icon('x')}</button></div>
        <h2 class="detail-title">${esc(t.title)}</h2>
        <dl class="kv">
          <dt>Assignee</dt><dd><span class="sel-wrap">${avatar(t.owner)}${isManager()
            ? `<select class="plain-select" id="f-owner" data-change="owner" data-id="${t.id}" aria-label="Assignee">${staffOpts}</select>`
            : `<b>${esc(person(t.owner).name)}</b>`}</span></dd>
          <dt>Priority</dt><dd><select class="pill-select ${t.priority}" id="f-priority" data-change="priority" data-id="${t.id}" aria-label="Priority">${prioOpts}</select></dd>
          <dt>Due Date</dt><dd>${icon('calendar-days')}<span>${fmtDate(t.due)}</span>${isOverdue(t) ? '<span class="pill bad">Overdue</span>' : ''}</dd>
          <dt>Status</dt><dd><select class="pill-select ${STATUS[t.status].cls}" id="f-status" data-change="status" data-id="${t.id}" aria-label="Status">${statusOpts}</select></dd>
          <dt>Assigned by</dt><dd>${avatar(t.by, 'av-sm')}<span>${esc(person(t.by).name)}</span></dd>
        </dl>
        ${t.desc ? `<div><h3 class="mini-h">Description</h3><p class="desc">${esc(t.desc)}</p></div>` : ''}
        <div>
          <div class="sub-h"><h3 class="mini-h">Subtasks (${done}/${t.subtasks.length})</h3><span class="bar"><i style="width:${p}%"${p === 100 ? ' class="ok"' : ''}></i></span><b class="pct">${p}%</b></div>
          ${subs ? `<ul class="subtasks">${subs}</ul>` : ''}
          <form class="inline-form" data-form="add-sub" data-id="${t.id}"><input id="f-sub" name="text" placeholder="Add a subtask" autocomplete="off" aria-label="New subtask" maxlength="120"><button class="icon-btn sm" aria-label="Add subtask">${icon('plus')}</button></form>
        </div>
        <div>
          <div class="sub-h"><h3 class="mini-h">Attachments (${t.files.length})</h3><button class="link" data-action="add-file" data-id="${t.id}">${icon('plus')} Add</button></div>
          ${t.files.length ? `<div class="files">${t.files.map(fileRow).join('')}</div>` : ''}
        </div>
        <div>
          <h3 class="mini-h">Comments</h3>
          ${t.comments.length ? `<div class="comments">${t.comments.map(commentRow).join('')}</div>` : ''}
          <form class="comment-form" data-form="comment" data-id="${t.id}"><input id="f-comment" name="text" placeholder="Write a comment..." autocomplete="off" aria-label="Write a comment" maxlength="500"><button class="icon-btn sm accent" aria-label="Send comment">${icon('send')}</button></form>
        </div>
        <div class="detail-actions">
          <button class="btn btn-primary" data-action="request-approval" data-id="${t.id}"${t.status === 'awaiting' || closed ? ' disabled' : ''}>${t.status === 'awaiting' ? 'Approval Requested' : 'Request Approval'}</button>
          <button class="btn btn-soft" data-action="complete" data-id="${t.id}"${closed ? ' disabled' : ''}>${closed ? 'Completed' : 'Mark as Complete'}</button>
        </div>
      </section>
      <section class="card activity">
        <h3 class="mini-h">Activity Log</h3>
        ${log ? `<ul>${log}</ul>` : '<p class="note">No activity yet.</p>'}
      </section>`;
  }

  // Colour and icon for a schedule entry: the course group for classes, blue for meetings.
  const EVENT_TONE = { upl: 'teal', arc: 'violet', grad: 'aw' };
  const eventIcon = (e) => (e.weekly ? 'graduation-cap' : e.online ? 'video' : 'users');

  function nextMeeting(e, cls) {
    const brk = breakOn(TODAY);
    if (!e) {
      return `<section class="card next-meet ${cls}"><span class="nm-ico">${icon('calendar-check')}</span><div><div class="eyebrow">Up Next</div><div class="nm-title">${brk ? esc(brk.label) : 'Nothing else scheduled today'}</div></div></section>`;
    }
    return `<section class="card next-meet ${cls}"><span class="nm-ico">${icon(e.weekly ? 'graduation-cap' : 'calendar-clock')}</span><div><div class="eyebrow">Up Next</div><div class="nm-time">${fmtClock(toMin(e.start))} – ${fmtClock(toMin(e.end))}</div><div class="nm-title">${esc(e.title)}</div><small>${icon(e.weekly ? 'book-open' : e.online ? 'video' : 'map-pin')}${esc(e.place)}</small></div></section>`;
  }

  function meetingRow(e) {
    const past = toMin(e.end) <= nowMin();
    return `<div class="li${past ? ' is-past' : ''}"><span class="li-time">${fmtClock(toMin(e.start))}<small>${fmtClock(toMin(e.end))}</small></span><span class="li-ico tone-${EVENT_TONE[e.kind] || 'ip'}">${icon(eventIcon(e))}</span><span class="li-main"><b>${esc(e.title)}</b><small>${esc(e.place)}</small></span><span></span></div>`;
  }

  function requestRow(r, withActions) {
    const c = REQ_CAT[r.cat] || REQ_CAT.maintenance;
    const acts = withActions && r.status === 'awaiting' && isManager()
      ? `<button class="btn btn-ok sm" data-action="decide" data-kind="request" data-id="${r.id}" data-ok="1">Approve</button><button class="btn btn-danger sm" data-action="decide" data-kind="request" data-id="${r.id}" data-ok="0">Reject</button>`
      : '';
    const meta = withActions ? ` · ${esc(r.location)} · ${esc(person(r.by).name)}` : '';
    return `<div class="li no-time"><span class="li-ico tone-${c.tone}">${icon(c.icon)}</span><span class="li-main"><b>${esc(r.title)}</b><small>#${esc(r.id)} · ${fmtDate(r.date)}${meta}</small></span><span class="li-actions">${reqPill(r.status)}${acts}</span></div>`;
  }

  /* ------------------------------------------------------------------ views */

  function viewMyDay() {
    const mine = myTasks();
    const open = mine.filter(isOpen);
    const top = open.slice().sort(byDue).slice(0, 6);
    const today = eventsOn(TODAY);
    const next = today.find((e) => toMin(e.end) > nowMin());
    const reqs = myRequests().slice().sort((a, b) => b.date.localeCompare(a.date) || b.id.localeCompare(a.id)).slice(0, 4);
    return `
    <div class="cols">
      <div class="col-main">
        ${managerUnverified() ? verifyCard() : ''}
        <div class="page-head">
          <div><h1>${greeting()}, ${esc(firstName(ME))}</h1><p class="sub">Here are your tasks and activities for today.</p></div>
          <span class="date-chip">${fmtLong(TODAY)} ${icon('calendar-days')}</span>
        </div>
        ${nextMeeting(next, 'only-narrow')}
        <div class="kpis">
          ${kpi({ icon: 'calendar-check', tone: 'ip', n: open.filter((t) => t.due === TODAY).length, label: 'Due Today', action: 'kpi', data: ' data-filter="due-today"' })}
          ${kpi({ icon: 'loader', tone: 'aw', n: mine.filter((t) => t.status === 'in-progress').length, label: 'In Progress', action: 'kpi', data: ' data-filter="in-progress"' })}
          ${kpi({ icon: 'clock', tone: 'warn', n: mine.filter((t) => t.status === 'awaiting').length, label: 'Awaiting Approval', action: 'kpi', data: ' data-filter="awaiting"' })}
          ${kpi({ icon: 'check', tone: 'ok', n: mine.filter((t) => t.status === 'completed').length, label: 'Completed', action: 'kpi', data: ' data-filter="completed"' })}
        </div>
        <section class="card cq">
          <div class="card-h"><h2 class="card-title caps">Today's Priorities</h2><button class="link" data-action="go" data-view="tasks">View All Tasks</button></div>
          ${taskTable(top, 'Nothing due in the next few days.')}
        </section>
        <div class="grid-2">
          <section class="card">
            <div class="card-h"><h2 class="card-title">Today's Schedule</h2><button class="link" data-action="go" data-view="calendar">View Calendar</button></div>
            <div class="list">${today.length ? today.map(meetingRow).join('') : `<div class="empty">${breakOn(TODAY) ? esc(breakOn(TODAY).label) : 'Nothing scheduled today.'}</div>`}</div>
          </section>
          <section class="card">
            <div class="card-h"><h2 class="card-title">Recent Service Requests</h2><button class="link" data-action="go" data-view="requests">View All</button></div>
            <div class="list">${reqs.length ? reqs.map((r) => requestRow(r, false)).join('') : '<div class="empty">You have not raised any requests yet.</div>'}</div>
          </section>
        </div>
      </div>
      <aside class="col-side" aria-label="Selected task">
        ${nextMeeting(next, 'only-wide')}
        ${detailWrap()}
      </aside>
    </div>`;
  }

  const TASK_FILTERS = [
    { id: 'all', label: 'All', test: () => true },
    { id: 'due-today', label: 'Due today', test: (t) => isOpen(t) && t.due === TODAY },
    { id: 'overdue', label: 'Overdue', test: isOverdue },
    { id: 'not-started', label: 'Not started', test: (t) => t.status === 'not-started' },
    { id: 'in-progress', label: 'In progress', test: (t) => t.status === 'in-progress' },
    { id: 'awaiting', label: 'Awaiting approval', test: (t) => t.status === 'awaiting' },
    { id: 'completed', label: 'Completed', test: (t) => t.status === 'completed' }
  ];

  function viewTasks() {
    const dept = isManager() && ui.taskScope === 'all';
    const scope = dept ? state.tasks.slice() : myTasks();
    const q = ui.search.trim().toLowerCase();
    const searched = q ? scope.filter((t) => `${t.title} ${person(t.owner).name}`.toLowerCase().includes(q)) : scope;
    const f = TASK_FILTERS.find((x) => x.id === ui.taskFilter) || TASK_FILTERS[0];
    const list = searched.filter(f.test).sort((a, b) =>
      (isOpen(b) - isOpen(a)) || (isOpen(a) ? byDue(a, b) : (b.completedOn || '').localeCompare(a.completedOn || '')));
    const overdue = scope.filter(isOverdue).length;
    return `
    <div class="cols">
      <div class="col-main">
        <div class="page-head">
          <div><h1>${dept ? 'Department Tasks' : 'My Tasks'}</h1><p class="sub">${plural(scope.filter(isOpen).length, 'open task')} · ${overdue} overdue</p></div>
          <div class="head-actions">
            ${isManager() ? `<div class="seg" role="group" aria-label="Whose tasks">
              <button class="${dept ? '' : 'is-on'}" data-action="scope" data-scope="mine" aria-pressed="${!dept}">Mine</button>
              <button class="${dept ? 'is-on' : ''}" data-action="scope" data-scope="all" aria-pressed="${dept}">Department</button>
            </div>` : ''}
            <button class="btn btn-primary" data-action="new-task">${icon('plus')} New Task</button>
          </div>
        </div>
        <div class="chips" role="group" aria-label="Filter tasks">${TASK_FILTERS.map((x) => `<button class="chip${x.id === f.id ? ' is-on' : ''}" data-action="task-filter" data-filter="${x.id}" aria-pressed="${x.id === f.id}">${x.label} <b>${searched.filter(x.test).length}</b></button>`).join('')}</div>
        ${q ? `<p class="search-note">Showing tasks matching “${esc(ui.search.trim())}” · <button class="link" data-action="clear-search">Clear search</button></p>` : ''}
        <section class="card cq">${taskTable(list, 'No tasks match this filter.')}</section>
      </div>
      <aside class="col-side" aria-label="Selected task">${detailWrap()}</aside>
    </div>`;
  }

  // Team members with their numbers: everyone in the shared database when the manager can read it,
  // otherwise the accounts on this device and their tasks here.
  function teamMembers() {
    if (cloud.host && cloud.docs.length) {
      const n = (v) => (Number.isFinite(Number(v)) ? Math.max(0, Math.round(Number(v))) : 0);
      return cloud.docs.filter((d) => d.account && typeof d.account === 'object').map((d) => {
        const s = d.stats && typeof d.stats === 'object' ? d.stats : {};
        knowPerson(d.id, d.account);
        return {
          id: d.id, name: String(d.account.name || 'Someone'), role: String(d.account.role || ''), dept: String(d.account.dept || ''),
          onSite: Date.parse(d.lastSeen || '') > Date.now() - 864e5, // active in the last 24 hours
          open: Math.max(0, n(s.tasks) - n(s.completed)), overdue: n(s.overdue), done: n(s.completed),
          notStarted: n(s.notStarted), inProgress: n(s.inProgress), awaiting: n(s.awaiting)
        };
      });
    }
    return D.staff.map((s) => {
      const theirs = state.tasks.filter((t) => t.owner === s.id);
      const by = (k) => theirs.filter((t) => t.status === k).length;
      return {
        id: s.id, name: s.name, role: s.role, dept: s.dept, onSite: s.onSite,
        open: theirs.filter(isOpen).length, overdue: theirs.filter(isOverdue).length, done: by('completed'),
        notStarted: by('not-started'), inProgress: by('in-progress'), awaiting: by('awaiting')
      };
    });
  }

  function viewTeam() {
    if (!isManager()) return managerOnly();
    const live = cloud.host && cloud.docs.length > 0;
    const members = teamMembers();
    const sum = (k) => members.reduce((t, m) => t + m[k], 0);
    const segs = [['not-started', 'notStarted'], ['in-progress', 'inProgress'], ['awaiting', 'awaiting'], ['completed', 'done']]
      .map(([k, f]) => ({ label: STATUS[k].label, value: sum(f), color: `var(--${STATUS[k].cls})` }));
    const total = segs.reduce((t, s) => t + s.value, 0);
    const wl = members.slice().sort((a, b) => b.open - a.open || b.overdue - a.overdue);
    const max = Math.max(1, ...wl.map((w) => w.open));
    const onSite = members.filter((m) => m.onSite).length;
    const here = live ? 'active today' : 'on campus';
    // With live data the task numbers live on the Users page; locally they open the department task list.
    const drill = (filter) => (live ? { action: 'go', data: ' data-view="users"' } : { action: 'team-filter', data: ` data-filter="${filter}"` });
    const steps = [
      { icon: 'user-plus', title: '1. Assign', text: 'Create or assign task', n: `${sum('notStarted')} not started` },
      { icon: 'clock', title: '2. Prioritize', text: 'Set priority & deadline', n: `${sum('overdue')} overdue` },
      { icon: 'circle-check', title: '3. Execute', text: 'Track progress', n: `${sum('inProgress')} in progress` },
      { icon: 'clipboard-check', title: '4. Review', text: 'Check & provide feedback', n: `${sum('awaiting')} awaiting` },
      { icon: 'archive', title: '5. Close', text: 'Complete & archive', n: `${sum('done')} completed` }
    ];
    const features = [['list-checks', 'Subtasks'], ['message-square', 'Comments'], ['paperclip', 'Attachments'], ['bell', 'Notifications'], ['users', 'Team Delegation'], ['badge-check', 'Manager Approvals']];

    return `
    <div class="stack">
      <div class="band"><h1>Manager Team Dashboard<span>· ${live ? 'everyone who signed up' : 'accounts on this device'}</span></h1>${live ? '<span class="live"><i></i>Live</span>' : '<span class="tag">Demo data</span>'}</div>
      <div class="kpis">
        ${kpi({ icon: 'users', tone: 'ip', n: members.length, label: `Total Staff · ${onSite} ${here}` })}
        ${kpi({ icon: 'hourglass', tone: 'warn', n: pendingApprovals().length, label: 'Pending Approvals', action: 'go', data: ' data-view="approvals"' })}
        ${kpi(Object.assign({ icon: 'circle-alert', tone: 'bad', n: sum('overdue'), label: 'Overdue Tasks' }, drill('overdue')))}
        ${kpi(Object.assign({ icon: 'layers', tone: 'ok', n: sum('open'), label: 'Active Tasks' }, drill('all')))}
      </div>
      <div class="grid-2">
        <section class="card">
          <div class="card-h"><h2 class="card-title">Team Workload</h2><div class="legend"><span><i style="background:var(--ip)"></i>Open</span><span><i style="background:var(--bad)"></i>Overdue</span></div></div>
          <div class="card-body workload">
            ${wl.length ? '' : '<p class="note">Nobody has signed up yet.</p>'}${wl.map((w) => {
              const onTime = Math.max(0, w.open - w.overdue);
              return `<div class="wl-row" title="${esc(w.name)}: ${w.open} open, ${w.overdue} overdue">
                <span class="wl-name">${avatar(w.id)}<span>${esc(w.name)}</span></span>
                <span class="wl-track"><span class="wl-fill" style="width:${(w.open / max) * 100}%">${onTime ? `<i style="flex:${onTime}"></i>` : ''}${w.overdue ? `<i class="od" style="flex:${w.overdue}"></i>` : ''}</span></span>
                <span class="wl-n">${w.open}${w.overdue ? `<small>(${w.overdue})</small>` : ''}</span>
              </div>`;
            }).join('')}
          </div>
        </section>
        <section class="card">
          <div class="card-h"><h2 class="card-title">Tasks by Status</h2></div>
          <div class="card-body donut-wrap">
            ${donut(segs, 172, 26, total, 'Tasks')}
            <ul class="dlegend">${segs.map((s) => `<li><i style="background:${s.color}"></i><span>${s.label}</span><b>${s.value}</b></li>`).join('')}</ul>
          </div>
        </section>
      </div>
      <section class="card">
        <div class="card-h"><h2 class="card-title">Task Workflow</h2></div>
        <div class="card-body">
          <div class="flow">${steps.map((s) => `<div class="flow-step"><span class="flow-ico">${icon(s.icon)}</span><span class="flow-txt"><b>${s.title}</b><small>${s.text}</small><span class="flow-n">${s.n}</span></span></div>`).join('')}</div>
          <div class="feature-chips">${features.map((f) => `<span>${icon(f[0])}${f[1]}</span>`).join('')}</div>
        </div>
      </section>
      <section class="card">
        <div class="card-h"><h2 class="card-title">Team Members</h2><span class="muted">${onSite} of ${members.length} ${here} · ${live ? 'from their sign-ins' : 'from gate & RFID logs'}</span></div>
        <div class="table-scroll">
          <table class="dtable">
            <thead><tr><th>Name</th><th>Department</th><th>Presence</th><th class="n">Open</th><th class="n">Overdue</th><th class="n">Completed</th></tr></thead>
            <tbody>${members.map((m) => `<tr><td><span class="who">${avatar(m.id, 'av-md')}<span>${esc(m.name)}<small>${esc(m.role)}</small></span></span></td><td>${esc(m.dept)}</td><td><span class="presence${m.onSite ? ' on' : ''}"><i></i>${m.onSite ? (live ? 'Active today' : 'On campus') : (live ? 'Not today' : 'Away')}</span></td><td class="n">${m.open}</td><td class="n">${m.overdue}</td><td class="n">${m.done}</td></tr>`).join('')}</tbody>
          </table>
        </div>
      </section>
    </div>`;
  }

  const HOUR_START = 8;
  const HOUR_END = 21; // Graduation Design runs to 9 pm

  function setCalDay(day) {
    ui.calDay = day;
    ui.week = Math.floor(daysBetween(weekStartOf(TODAY), weekStartOf(day)) / 7);
    render();
  }

  function miniMonth() {
    const d = toDate(ui.calDay);
    const y = d.getUTCFullYear();
    const m = d.getUTCMonth();
    const first = isoOf(new Date(Date.UTC(y, m, 1)));
    const daysIn = new Date(Date.UTC(y, m + 1, 0)).getUTCDate();
    const lead = (dowOf(first) + 1) % 7;
    const weeks = Math.ceil((lead + daysIn) / 7);
    const start = weekStartOf(first);
    const cells = [];
    for (let i = 0; i < weeks * 7; i++) {
      const day = addDays(start, i);
      const dd = toDate(day);
      const cls = [dd.getUTCMonth() !== m ? 'out' : '', eventsOn(day).length ? 'has' : '', day === TODAY ? 'is-today' : '', day === ui.calDay ? 'is-sel' : ''].filter(Boolean).join(' ');
      cells.push(`<button class="${cls}" data-action="cal-day" data-day="${day}" aria-label="${fmtLong(day)}">${dd.getUTCDate()}</button>`);
    }
    return `<div class="sub-h"><h3 class="mini-h">${MONTHS_LONG[m]} ${y}</h3></div>
      <div class="month">${['Sat', 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri'].map((w) => `<span class="dow">${w}</span>`).join('')}${cells.join('')}</div>`;
  }

  function viewCalendar() {
    const ws = addDays(weekStartOf(TODAY), ui.week * 7);
    const days = Array.from({ length: 7 }, (_, i) => addDays(ws, i));
    const rows = HOUR_END - HOUR_START;
    const hours = [];
    for (let h = HOUR_START; h < HOUR_END; h++) hours.push(h);
    const a = toDate(days[0]);
    const b = toDate(days[6]);
    const range = `${a.getUTCDate()} ${MONTHS[a.getUTCMonth()]} – ${b.getUTCDate()} ${MONTHS[b.getUTCMonth()]} ${b.getUTCFullYear()}`;

    const head = days.map((d) => {
      const brk = breakOn(d);
      return `<div class="wk-head${d === TODAY ? ' is-today' : ''}${d === ui.calDay ? ' is-sel' : ''}"><button data-action="cal-day" data-day="${d}" aria-label="${fmtLong(d)}"><small>${DOW[dowOf(d)]}</small><b>${toDate(d).getUTCDate()}</b></button>${brk ? `<em class="wk-note">${esc(brk.label)}</em>` : ''}</div>`;
    }).join('');
    const nowAt = nowMin();
    const cols = days.map((d) => {
      const evs = eventsOn(d).map((e) => {
        const s = Math.max(toMin(e.start), HOUR_START * 60);
        const en = Math.min(toMin(e.end), HOUR_END * 60);
        const top = (s - HOUR_START * 60) / 60;
        const h = Math.max(en - s, 30) / 60;
        return `<button class="ev ${e.kind}" style="top:calc(var(--hour) * ${top} + 2px);height:calc(var(--hour) * ${h} - 4px)" data-action="cal-day" data-day="${d}" title="${esc(e.title)} · ${e.start}–${e.end} · ${esc(e.place)}"><b>${esc(e.title)}</b><small>${e.start} – ${e.end}${e.weekly ? ` · ${esc(e.place)}` : ''}</small></button>`;
      }).join('');
      const showNow = d === TODAY && nowAt >= HOUR_START * 60 && nowAt <= HOUR_END * 60;
      const now = showNow ? `<span class="now-line" style="top:calc(var(--hour) * ${((nowAt - HOUR_START * 60) / 60).toFixed(3)})" aria-hidden="true"></span>` : '';
      const dow = dowOf(d);
      return `<div class="wk-day${dow === 5 ? ' is-weekend' : ''}${breakOn(d) ? ' is-break' : ''}${d === TODAY ? ' is-today' : ''}" style="height:calc(var(--hour) * ${rows})">${evs}${now}</div>`;
    }).join('');

    const agendaEvents = eventsOn(ui.calDay);
    const agendaTasks = myTasks().filter((t) => isOpen(t) && t.due === ui.calDay);
    const dayBreak = breakOn(ui.calDay);
    const agenda = (dayBreak ? [`<div class="agenda-item"><span class="t break">Off</span><span>${esc(dayBreak.label)}</span></div>`] : [])
      .concat(agendaEvents.map((e) => `<div class="agenda-item"><span class="t ${e.kind}">${e.start}</span><span>${esc(e.title)}${e.weekly ? ` <small class="muted">${esc(e.place)}</small>` : ''}</span></div>`))
      .concat(agendaTasks.map((t) => `<div class="agenda-item"><span class="t task">Due</span><span>${esc(t.title)}</span></div>`)).join('');
    const unread = D.emails.filter((m) => m.unread).length;

    return `
    <div class="stack">
      <div class="page-head">
        <div><h1>Calendar</h1><p class="sub">Weekly lectures, meetings and deadlines.</p>
          <div class="legend cal-legend"><span><i style="background:var(--teal)"></i>UPL courses</span><span><i style="background:var(--violet)"></i>ARC courses</span><span><i style="background:var(--aw)"></i>Graduation Design</span><span><i style="background:var(--ip)"></i>Meetings</span></div></div>
        <div class="cal-nav">
          <button class="icon-btn" data-action="week" data-step="-1" aria-label="Previous week">${icon('chevron-left')}</button>
          <b>${range}</b>
          <button class="icon-btn" data-action="week" data-step="1" aria-label="Next week">${icon('chevron-right')}</button>
          <button class="btn btn-ghost sm" data-action="week" data-step="0">Today</button>
        </div>
      </div>
      <section class="card">
        <div class="week-scroll">
          <div class="week">
            <div class="wk-head"></div>${head}
            <div class="wk-hours" style="height:calc(var(--hour) * ${rows})">${hours.map((h) => `<span style="top:calc(var(--hour) * ${h - HOUR_START})">${pad(h)}:00</span>`).join('')}</div>
            ${cols}
          </div>
        </div>
      </section>
      <div class="band"><h2>Email &amp; Calendar Integrations</h2><span class="tag">Demo data</span></div>
      <div class="grid-3">
        <section class="card">
          <div class="card-h"><h2 class="card-title">Email Integration</h2><span class="pill ip">${unread} unread</span></div>
          <div class="card-body">${D.emails.map((m) => `<div class="mail${m.unread ? '' : ' read'}${m.flag ? ' flag' : ''}">${icon('mail')}<div><b>${esc(m.subject)}</b><small>from: ${esc(m.from)}</small></div><time>${esc(m.at)}</time></div>`).join('')}</div>
        </section>
        <section class="card">
          <div class="card-h"><h2 class="card-title">Calendar View</h2><span class="muted">${fmtShort(ui.calDay)}</span></div>
          <div class="card-body">
            ${miniMonth()}
            <div class="agenda">${agenda || '<p class="note">Nothing scheduled on this day.</p>'}</div>
          </div>
        </section>
        <section class="card">
          <div class="card-h"><h2 class="card-title">Quick Add</h2></div>
          <div class="card-body">
            <form class="form" data-form="quick-add">
              <div class="field"><label for="qa-type">Type</label><select class="select" id="qa-type" name="type"><option value="task">Task</option><option value="meeting">Meeting</option><option value="request">Service request</option></select></div>
              <div class="field"><label for="qa-title">Title</label><input class="input" id="qa-title" name="title" placeholder="Add a new task..." required maxlength="120" autocomplete="off"></div>
              <div class="field-row">
                <div class="field"><label for="qa-date">Date</label><input class="input" type="date" id="qa-date" name="date" value="${ui.calDay}"></div>
                <div class="field"><label for="qa-time">Time</label><input class="input" type="time" id="qa-time" name="time" value="15:00"></div>
              </div>
              <button class="btn btn-primary">${icon('plus')} Add</button>
            </form>
          </div>
        </section>
      </div>
    </div>`;
  }

  const REQ_FILTERS = [
    { id: 'all', label: 'All', test: () => true },
    { id: 'open', label: 'In progress', test: (r) => r.status === 'in-progress' },
    { id: 'awaiting', label: 'Awaiting approval', test: (r) => r.status === 'awaiting' },
    { id: 'closed', label: 'Closed', test: (r) => ['approved', 'completed', 'rejected'].includes(r.status) }
  ];

  function viewRequests() {
    const q = ui.search.trim().toLowerCase();
    const base = myRequests();
    const searched = q ? base.filter((r) => `${r.id} ${r.title} ${r.location} ${person(r.by).name}`.toLowerCase().includes(q)) : base;
    const f = REQ_FILTERS.find((x) => x.id === ui.reqFilter) || REQ_FILTERS[0];
    const list = searched.filter(f.test).slice().sort((a, b) => b.date.localeCompare(a.date) || b.id.localeCompare(a.id));
    const n = (id) => base.filter(REQ_FILTERS.find((x) => x.id === id).test).length;
    return `
    <div class="stack">
      <div class="page-head">
        <div><h1>${isManager() ? 'Service Requests' : 'My Service Requests'}</h1><p class="sub">${isManager() ? 'Maintenance, IT and equipment requests across the faculty.' : 'Requests you raised. Only you and the manager can see them.'}</p></div>
        <button class="btn btn-primary" data-action="new-request">${icon('plus')} New Request</button>
      </div>
      <div class="kpis kpis-3">
        ${kpi({ icon: 'wrench', tone: 'ip', n: n('open'), label: 'In Progress', action: 'req-filter', data: ' data-filter="open"' })}
        ${kpi({ icon: 'hourglass', tone: 'warn', n: n('awaiting'), label: 'Awaiting Approval', action: 'req-filter', data: ' data-filter="awaiting"' })}
        ${kpi({ icon: 'circle-check', tone: 'ok', n: n('closed'), label: 'Closed This Month', action: 'req-filter', data: ' data-filter="closed"' })}
      </div>
      <div class="chips" role="group" aria-label="Filter requests">${REQ_FILTERS.map((x) => `<button class="chip${x.id === f.id ? ' is-on' : ''}" data-action="req-filter" data-filter="${x.id}" aria-pressed="${x.id === f.id}">${x.label} <b>${searched.filter(x.test).length}</b></button>`).join('')}</div>
      ${q ? `<p class="search-note">Showing requests matching “${esc(ui.search.trim())}” · <button class="link" data-action="clear-search">Clear search</button></p>` : ''}
      <section class="card"><div class="list">${list.length ? list.map((r) => requestRow(r, true)).join('') : `<div class="empty">${icon('inbox')}<p>No requests match this filter.</p></div>`}</div></section>
    </div>`;
  }

  function viewApprovals() {
    if (!isManager()) return managerOnly();
    const items = pendingApprovals();
    const cards = items.map(({ kind, item }) => {
      if (kind === 'task') {
        return `<section class="card appr">
          <span class="li-ico tone-aw">${icon('square-check-big')}</span>
          <div><h3>${esc(item.title)}</h3>
            <div class="meta"><span>${icon('tag')}Task</span><span>${avatar(item.owner, 'av-sm')}${esc(person(item.owner).name)}</span><span>${icon('calendar-days')}Due ${fmtDate(item.due)}</span>${isOverdue(item) ? '<span class="pill bad">Overdue</span>' : ''}</div>
            ${item.desc ? `<p>${esc(item.desc)}</p>` : ''}</div>
          <div class="acts"><button class="btn btn-ok sm" data-action="decide" data-kind="task" data-id="${item.id}" data-ok="1">${icon('check')}Approve</button><button class="btn btn-danger sm" data-action="decide" data-kind="task" data-id="${item.id}" data-ok="0">${icon('undo-2')}Return</button></div>
        </section>`;
      }
      const c = REQ_CAT[item.cat] || REQ_CAT.maintenance;
      return `<section class="card appr">
        <span class="li-ico tone-${c.tone}">${icon(c.icon)}</span>
        <div><h3>${esc(item.title)}</h3>
          <div class="meta"><span>${icon('tag')}Service request #${esc(item.id)}</span><span>${avatar(item.by, 'av-sm')}${esc(person(item.by).name)}</span><span>${icon('map-pin')}${esc(item.location)}</span><span>${icon('calendar-days')}Submitted ${fmtDate(item.date)}</span></div>
          ${item.desc ? `<p>${esc(item.desc)}</p>` : ''}</div>
        <div class="acts"><button class="btn btn-ok sm" data-action="decide" data-kind="request" data-id="${item.id}" data-ok="1">${icon('check')}Approve</button><button class="btn btn-danger sm" data-action="decide" data-kind="request" data-id="${item.id}" data-ok="0">${icon('x')}Reject</button></div>
      </section>`;
    }).join('');
    const history = state.decisions.slice(0, 8).map((d) => `<div class="li no-time"><span class="li-ico tone-${d.approve ? 'ok' : 'bad'}">${icon(d.approve ? 'check' : 'undo-2')}</span><span class="li-main"><b>${esc(d.title)}</b><small>${d.kind === 'task' ? 'Task' : 'Service request'} · ${d.approve ? 'Approved' : d.kind === 'task' ? 'Returned for changes' : 'Rejected'}</small></span><span class="li-actions"><span class="muted">${esc(d.at)}</span></span></div>`).join('');
    return `
    <div class="stack">
      <div class="page-head"><div><h1>Approvals</h1><p class="sub">${items.length ? `${plural(items.length, 'item')} waiting for a decision.` : 'Nothing is waiting for a decision.'}</p></div></div>
      ${cards || `<section class="card empty">${icon('badge-check')}<b>All caught up</b><span>New approval requests from your team will show up here.</span></section>`}
      ${history ? `<section class="card"><div class="card-h"><h2 class="card-title">Recent Decisions</h2></div><div class="list">${history}</div></section>` : ''}
    </div>`;
  }

  function viewReports() {
    const mine = myTasks();
    const doneToday = mine.filter((t) => t.status === 'completed' && t.completedOn === TODAY);
    const outstanding = mine.filter(isOpen).sort(byDue);
    const upcoming = outstanding.filter((t) => { const n = daysBetween(TODAY, t.due); return n >= 1 && n <= 4; });
    const tomorrow = addDays(TODAY, 1);
    const tmr = eventsOn(tomorrow);
    const mgr = isManager();
    const all = mgr ? state.tasks : mine; // staff report on their own tasks only
    const count = (s) => all.filter((t) => t.status === s).length;
    const chart = [
      { label: 'To Do', value: count('not-started'), color: 'var(--ns)' },
      { label: 'In Progress', value: count('in-progress'), color: 'var(--ip)' },
      { label: 'Awaiting', value: count('awaiting'), color: 'var(--aw)' },
      { label: 'Completed', value: count('completed'), color: 'var(--ok)' },
      { label: 'Overdue', value: all.filter(isOverdue).length, color: 'var(--bad)' }
    ];
    const members = D.staff.map((s) => {
      const theirs = all.filter((t) => t.owner === s.id);
      const done = theirs.filter((t) => t.status === 'completed').length;
      return { id: s.id, total: theirs.length, done, overdue: theirs.filter(isOverdue).length, rate: theirs.length ? Math.round((100 * done) / theirs.length) : 0 };
    }).filter((m) => m.total).sort((a, b) => b.rate - a.rate || b.total - a.total);
    const li = (ic, text, right) => `<li>${icon(ic)}<span>${text}</span>${right ? `<small>${right}</small>` : ''}</li>`;

    return `
    <div class="stack">
      <div class="page-head">
        <div><h1>End-of-Day Report</h1><p class="sub">${fmtLong(TODAY)} · summary as of ${nowLabel()}</p></div>
      </div>
      <div class="grid-4">
        <section class="card sum-card">
          <div class="sum-head"><span class="li-ico tone-ok">${icon('circle-check')}</span><span class="eyebrow">Completed today</span></div>
          <span class="big">${doneToday.length}</span>
          <ul>${doneToday.slice(0, 4).map((t) => li('check', esc(t.title))).join('') || '<li class="muted">Nothing completed yet.</li>'}</ul>
        </section>
        <section class="card sum-card">
          <div class="sum-head"><span class="li-ico tone-ip">${icon('list-todo')}</span><span class="eyebrow">Outstanding work</span></div>
          <span class="big">${outstanding.length}</span>
          <ul>${outstanding.slice(0, 4).map((t) => li('circle-dot', esc(t.title), fmtShort(t.due))).join('') || '<li class="muted">All clear.</li>'}</ul>
        </section>
        <section class="card sum-card">
          <div class="sum-head"><span class="li-ico tone-aw">${icon('calendar-clock')}</span><span class="eyebrow">Upcoming deadlines</span></div>
          <span class="big">${upcoming.length}</span>
          <ul>${upcoming.slice(0, 4).map((t) => li('flag', esc(t.title), fmtShort(t.due))).join('') || '<li class="muted">No deadlines in the next 4 days.</li>'}</ul>
        </section>
        <section class="card sum-card">
          <div class="sum-head"><span class="li-ico tone-violet">${icon('calendar-days')}</span><span class="eyebrow">Tomorrow · ${fmtShort(tomorrow)}</span></div>
          <span class="big">${tmr.length}</span>
          <ul>${tmr.map((e) => li('clock', esc(e.title), e.start)).join('') || '<li class="muted">No meetings scheduled.</li>'}</ul>
        </section>
      </div>
      ${mgr ? '<div class="grid-2">' : ''}
        <section class="card">
          <div class="card-h"><h2 class="card-title">Task Status · ${mgr ? 'Department' : 'My tasks'}</h2><span class="muted">${plural(all.length, 'task')}</span></div>
          <div class="card-body">${barChart(chart)}</div>
        </section>
        ${mgr ? `<section class="card">
          <div class="card-h"><h2 class="card-title">Completion by Team Member</h2></div>
          <div class="table-scroll">
            <table class="dtable">
              <thead><tr><th>Name</th><th class="n">Tasks</th><th class="n">Done</th><th class="n">Overdue</th><th>Completion</th></tr></thead>
              <tbody>${members.map((m) => `<tr><td><span class="who">${avatar(m.id)}<span>${esc(person(m.id).name)}</span></span></td><td class="n">${m.total}</td><td class="n">${m.done}</td><td class="n">${m.overdue}</td><td style="min-width:120px"><div class="prog"><span class="prog-n">${m.rate}%</span><span class="bar"><i style="width:${m.rate}%"${m.rate === 100 ? ' class="ok"' : ''}></i></span></div></td></tr>`).join('')}</tbody>
            </table>
          </div>
        </section>
      </div>` : ''}
    </div>`;
  }

  // HC-SR04 distance reading for a slot: short when a car is parked over the sensor.
  const sensorCm = (n, occ) => (occ ? 32 + ((n * 37) % 41) : 178 + ((n * 53) % 37));
  const zoneOf = (n) => { let start = 1; for (const z of D.campus.parkingZones) { if (n < start + z.slots) return z; start += z.slots; } return D.campus.parkingZones[0]; };

  function viewCampus() {
    const occ = campus.occupied.size;
    const pct = Math.round((100 * occ) / TOTAL_SLOTS);
    const people = campus.occupancy.reduce((n, a) => n + a.people, 0);
    let start = 1;
    const zones = D.campus.parkingZones.map((z) => {
      const nums = Array.from({ length: z.slots }, (_, i) => start + i);
      start += z.slots;
      const taken = nums.filter((n) => campus.occupied.has(n)).length;
      return `<div class="lot-zone"><h4>${esc(z.label)} · ${taken}/${z.slots} taken</h4><div class="lot">${nums.map((n) => {
        const mine = n === D.campus.mySlot;
        const isOcc = campus.occupied.has(n);
        const cls = `slot${isOcc && !mine ? ' occ' : ''}${mine ? ' mine' : ''}${n === campus.slot ? ' is-sel' : ''}`;
        return `<button class="${cls}" data-action="slot" data-n="${n}" aria-label="P${n}: ${mine ? 'your space' : isOcc ? 'occupied' : 'free'}">${isOcc ? icon('car') : ''}<span>P${n}</span></button>`;
      }).join('')}</div></div>`;
    }).join('');
    const sel = campus.slot;
    const selOcc = campus.occupied.has(sel);
    const selMine = sel === D.campus.mySlot;
    const steps = D.campus.journey.map((s, i) => {
      const st = i < D.campus.journeyStep ? 'done' : i === D.campus.journeyStep ? 'now' : 'todo';
      return `<div class="j-step ${st}"><div class="j-top"><span class="j-num">${st === 'done' ? icon('check') : i + 1}</span>${icon(s.icon, 'j-ico')}</div><b>${esc(s.title)}</b><p>${esc(s.text)}</p><time>${st === 'now' ? 'In progress' : st === 'done' ? s.time : `Planned ${s.time}`}</time></div>`;
    }).join('');
    const log = campus.accessLog.slice(0, 7).map((e) => `<div class="li"><span class="li-time">${esc(e.time)}</span>${e.who ? avatar(e.who) : '<span class="av" style="--h:0" aria-hidden="true">?</span>'}<span class="li-main"><b>${e.who ? esc(person(e.who).name) : esc(e.label || 'Unknown vehicle')}</b><small>${esc(e.method)} · ${esc(e.point)}</small></span><span class="pill ${e.result === 'granted' ? 'ok' : 'bad'}">${e.result === 'granted' ? 'Granted' : 'Denied'}</span></div>`).join('');
    const devices = D.campus.devices.map((d) => {
      const allUp = d.online === d.total;
      return `<div class="device"><span class="d-ico">${icon(d.icon)}</span><div><b>${esc(d.name)}</b><small>${esc(d.role)}</small><span class="d-detail">${esc(d.detail)}</span><span class="presence ${allUp ? 'on' : 'warn'}"><i></i>${allUp ? `Online${d.total > 1 ? ` · ${d.online}/${d.total}` : ''}` : `${d.online}/${d.total} online`}</span></div></div>`;
    }).join('');

    return `
    <div class="stack">
      <div class="band"><h1>Smart Campus<span>· ESP32 &amp; sensors</span></h1>
        <button class="live${campus.live ? '' : ' paused'}" data-action="toggle-live" aria-pressed="${campus.live}"><i></i>${campus.live ? 'Live · simulated' : 'Paused'}</button>
      </div>
      <div class="kpis">
        ${kpi({ icon: 'square-parking', tone: 'ip', n: `${pct}%`, label: `Parking · ${occ} of ${TOTAL_SLOTS} spaces taken` })}
        ${kpi({ icon: 'door-open', tone: campus.gate.open ? 'ok' : 'nav', n: campus.gate.open ? 'Open' : 'Closed', label: `${campus.gate.name} · last opened ${campus.gate.last}` })}
        ${kpi({ icon: 'activity', tone: 'teal', n: people, label: 'People in buildings · PIR' })}
        ${kpi({ icon: 'thermometer', tone: 'violet', n: `${campus.env.temp.toFixed(1)}°C`, label: `${campus.env.room} · ${campus.env.humidity}% humidity` })}
      </div>
      <section class="card">
        <div class="card-h"><h2 class="card-title">Today's Arrival Journey · ${esc(person(ME).name)}</h2><span class="muted">Stage ${D.campus.journeyStep + 1} of ${D.campus.journey.length}</span></div>
        <div class="card-body"><div class="journey">${steps}</div></div>
      </section>
      <div class="grid-2">
        <section class="card">
          <div class="card-h"><h2 class="card-title">Parking Occupancy</h2>
            <div class="legend"><span><i style="background:var(--ok)"></i>Free</span><span><i style="background:var(--ns)"></i>Occupied</span><span><i style="background:var(--accent)"></i>Your space</span></div></div>
          <div class="card-body">
            <div class="park-top">
              ${donut([{ label: 'Occupied', value: occ, color: 'var(--ns)' }, { label: 'Free', value: TOTAL_SLOTS - occ, color: 'var(--ok)' }], 120, 18, `${pct}%`, 'occupied')}
              <ul class="dlegend">
                <li><i style="background:var(--ns)"></i><span>Occupied</span><b>${occ}</b></li>
                <li><i style="background:var(--ok)"></i><span>Free</span><b>${TOTAL_SLOTS - occ}</b></li>
                <li><i style="background:var(--accent)"></i><span>Your space${myPlate() ? ` · ${esc(myPlate())}` : ''}</span><b>P${D.campus.mySlot}</b></li>
              </ul>
            </div>
            ${zones}
            <div class="slot-info">${icon('radar')}<span><b>P${sel}</b> · ${esc(zoneOf(sel).label)} · HC-SR04 reads <b>${sensorCm(sel, selOcc || selMine)} cm</b> → ${selMine ? `assigned to you${myPlate() ? ` (${esc(myPlate())})` : ''}` : selOcc ? 'occupied' : 'free'}</span></div>
          </div>
        </section>
        <div class="stack">
          ${isManager() ? `<section class="card">
            <div class="card-h"><h2 class="card-title">Gate &amp; Access Log</h2>
              <button class="btn btn-soft sm" data-action="gate"${campus.gate.open ? ' disabled' : ''}>${icon('door-open')}${campus.gate.open ? 'Gate open' : 'Open gate'}</button></div>
            <div class="list">${log}</div>
          </section>` : ''}
          <section class="card">
            <div class="card-h"><h2 class="card-title">Building Occupancy</h2><span class="muted">PIR motion sensors</span></div>
            <div class="card-body">${campus.occupancy.map((a) => { const p = Math.round((100 * a.people) / a.capacity); return `<div class="occ-row"><span>${esc(a.area)}</span><span class="bar"><i style="width:${p}%"></i></span><span class="n"><b>${a.people}</b> / ${a.capacity}</span></div>`; }).join('')}</div>
          </section>
        </div>
      </div>
      <section class="card">
        <div class="card-h"><h2 class="card-title">ESP32 &amp; Sensors</h2><span class="muted">IoT hardware for the smart campus</span></div>
        <div class="card-body"><div class="devices">${devices}</div></div>
      </section>
      <section class="card">
        <div class="card-h"><h2 class="card-title">System Architecture</h2></div>
        <div class="card-body">
          <div class="arch">
            <div class="arch-node"><span class="an-ico">${icon('cpu')}</span><div><b>ESP32 + sensors</b><small>RFID · HC-SR04 · ESP32-CAM · PIR · DHT22</small></div></div>
            <span class="arch-arrow" aria-label="sends over Wi-Fi to">${icon('wifi')}${icon('arrow-right')}</span>
            <div class="arch-node"><span class="an-ico">${icon('cloud')}</span><div><b>Secure API</b><small>Cloud / local server</small></div></div>
            <span class="arch-arrow" aria-label="feeds">${icon('arrow-right')}</span>
            <div class="arch-out">
              <div class="arch-node"><span class="an-ico">${icon('smartphone')}</span><div><b>Staff Mobile App</b><small>Task manager &amp; parking navigation</small></div></div>
              <div class="arch-node"><span class="an-ico">${icon('monitor')}</span><div><b>Manager Dashboard</b><small>Monitoring &amp; reports</small></div></div>
            </div>
          </div>
        </div>
      </section>
    </div>`;
  }

  function viewPreferences() {
    const sw = (key, title, text) => `<div class="pref-row"><div><b>${title}</b><small>${text}</small></div><button class="switch" role="switch" aria-checked="${ui.prefs[key]}" aria-label="${title}" data-action="pref" data-key="${key}"></button></div>`;
    const me = person(ME);
    const acct = currentAccount();
    const plate = myPlate();
    return `
    <div class="stack" style="max-width:760px">
      <div class="page-head"><div><h1>Settings</h1><p class="sub">Preferences are saved in this browser.</p></div></div>
      <section class="card">
        <div class="pref-row"><div class="who-row">${avatar(ME, 'av-lg')}<div><b>${esc(me.name)}</b><small>${esc(me.role)} · ${esc(me.dept)}</small></div></div>
          ${plate ? `<span class="pill ip">${icon('car')}${esc(plate)} · P${D.campus.mySlot}</span>` : `<span class="pill ns">${icon('credit-card')}RFID card access</span>`}</div>
        <div class="pref-row"><div><b>Email</b><small>${acct ? esc(acct.email) : ''}</small></div>${acct ? `<span class="pill ns">Staff ID ${esc(acct.staffId)}</span>` : ''}</div>
        ${isManager()
          ? `<div class="pref-row"><div><b>Role · Manager</b><small>You can see every user's statistics and activity. Users never see each other's data.</small></div>${onPlatform || FB ? `<button class="btn btn-soft" data-action="go" data-view="users">${icon('chart-pie')}Open Users</button>` : ''}</div>`
          : managerUnverified()
            ? `<div class="pref-row"><div><b>Role · Manager (email not confirmed)</b><small>Confirm your email to open the manager pages.</small></div><button class="btn btn-soft" data-action="fb-recheck">${icon('check')}I've confirmed</button></div>`
            : `<div class="pref-row"><div><b>Role · Staff</b><small>You see only your own tasks, requests and report. Other users cannot see yours.${cloud.db || FB ? ' The manager can see your statistics and activity.' : ''}</small></div><span class="pill ns">${icon('shield-check')}Private</span></div>`}
        <div class="pref-row"><div><b>Sign out</b><small>Go back to the sign-in screen on this device.</small></div>
          <button class="btn btn-ghost" data-action="sign-out">${icon('log-out')}Sign out</button></div>
      </section>
      <section class="card">
        <div class="pref-row"><div><b>Theme</b><small>Follow your device or pick one.</small></div>
          <div class="seg" role="group" aria-label="Theme">${['system', 'light', 'dark'].map((t) => `<button class="${ui.theme === t ? 'is-on' : ''}" data-action="set-theme" data-theme="${t}" aria-pressed="${ui.theme === t}">${t[0].toUpperCase() + t.slice(1)}</button>`).join('')}</div></div>
        ${sw('email', 'Email notifications', 'Comments, approvals and new assignments.')}
        ${sw('digest', 'End-of-day summary', 'Completed work and tomorrow’s schedule at 17:00.')}
        ${sw('parking', 'Parking alerts', 'Notify me when a parking space is assigned at the gate.')}
      </section>
      <section class="card">
        <div class="pref-row"><div><b>Demo data</b><small>This dashboard runs on sample data for ${fmtLong(TODAY)}. Your changes are kept in this browser.</small></div>
          <button class="btn ${ui.confirmReset ? 'btn-danger' : 'btn-ghost'}" data-action="reset">${icon('rotate-ccw')}${ui.confirmReset ? 'Click again to reset' : 'Reset demo data'}</button></div>
      </section>
    </div>`;
  }

  const VIEWS = {
    'my-day': viewMyDay,
    tasks: viewTasks,
    team: viewTeam,
    calendar: viewCalendar,
    requests: viewRequests,
    approvals: viewApprovals,
    reports: viewReports,
    campus: viewCampus,
    preferences: viewPreferences,
    activity: viewActivity,
    users: viewUsers
  };

  /* ------------------------------------------------------------------ shell */

  const NAV = [
    { id: 'my-day', label: 'My Day', icon: 'house' },
    { id: 'tasks', label: 'My Tasks', icon: 'square-check-big' },
    { id: 'team', label: 'Team', icon: 'users' },
    { id: 'calendar', label: 'Calendar', icon: 'calendar-days' },
    { id: 'requests', label: 'Service Requests', icon: 'file-text' },
    { id: 'approvals', label: 'Approvals', icon: 'circle-check-big' },
    { id: 'reports', label: 'Reports', icon: 'chart-column' },
    { id: 'campus', label: 'Smart Campus', icon: 'radar' }
  ];
  const BOTTOM = ['my-day', 'tasks', 'calendar'];
  // The host (page owner) also gets the Activity page.
  const MANAGER_ONLY = ['team', 'approvals'];
  const MANAGER_NAV = [
    { id: 'users', label: 'Users', icon: 'chart-pie', tag: 'Manager' },
    { id: 'activity', label: 'Activity', icon: 'eye', tag: 'Manager' }
  ];
  // Staff never get pages about other people; the manager also gets the real-user pages.
  const navItems = () => {
    if (!isManager()) return NAV.filter((n) => !MANAGER_ONLY.includes(n.id));
    return onPlatform || FB ? NAV.concat(MANAGER_NAV) : NAV;
  };
  const viewLabel = (v) => ({ preferences: 'Settings', activity: 'Activity', users: 'Users' }[v] || (NAV.find((n) => n.id === v) || {}).label || v);

  function shell() {
    return `
    <div class="app" id="app">
      <aside class="sidebar" aria-label="Main navigation">
        <div class="brand"><span class="brand-mark">${icon('building-2')}</span><span class="brand-text"><b>ASU FOE</b><small>Smart Staff Experience</small></span></div>
        <span class="demo-chip">${icon('circle-dot')}Demo data</span>
        <nav class="nav" id="nav"></nav>
        <div class="side-foot" id="side-foot"></div>
      </aside>
      <div class="main">
        <header class="topbar">
          <span class="top-brand" aria-hidden="true">${icon('building-2')}</span>
          <div class="search" id="search-wrap">
            ${icon('search', 'search-ico')}
            <input id="search" type="search" placeholder="Search tasks, requests, people..." autocomplete="off" aria-label="Search tasks, requests and people">
            <div class="pop search-pop" id="search-pop" hidden></div>
          </div>
          <div class="top-actions">
            <button class="icon-btn theme-btn" data-action="toggle-theme" aria-label="Switch light or dark theme" id="theme-btn"></button>
            <div class="bell-wrap">
              <button class="icon-btn" data-action="toggle-bell" aria-label="Notifications" aria-haspopup="true" id="bell-btn">${icon('bell')}<span class="dot" id="bell-dot" hidden></span></button>
              <div class="pop bell-pop" id="bell-pop" hidden></div>
            </div>
            <button class="user" data-action="go" data-view="preferences" aria-label="Your profile and settings">${avatar(ME, 'av-md')}<span class="user-txt"><b>${esc(person(ME).name)}</b><small id="user-role">${esc(person(ME).role)}</small></span>${icon('chevron-down', 'user-chev')}</button>
          </div>
        </header>
        <main class="view" id="view" tabindex="-1"></main>
      </div>
      <nav class="bottom-nav" id="bottom-nav" aria-label="Main navigation"></nav>
      <div class="scrim" data-action="close-drawer"></div>
      <div id="sheet-host"></div>
      <div id="modal-host"></div>
      <input type="file" id="file-input" multiple hidden>
      <div class="toast-host" id="toast-host" aria-live="polite"></div>
    </div>`;
  }

  function navButton(n, extraCls) {
    const on = ui.view === n.id;
    const badge = n.tag ? `<span class="host-tag">${n.tag}</span>`
      : n.id === 'approvals' && pendingApprovals().length ? `<span class="badge">${pendingApprovals().length}</span>` : '';
    return `<button class="nav-item${on ? ' is-active' : ''}${extraCls || ''}" data-action="go" data-view="${n.id}"${on ? ' aria-current="page"' : ''}>${icon(n.icon)}<span>${n.label}</span>${badge}</button>`;
  }

  function renderChrome() {
    $('#nav').innerHTML = navItems().map((n) => navButton(n)).join('');
    $('#side-foot').innerHTML = `
      ${myPlate()
        ? `<button class="park-mini" data-action="go" data-view="campus"><span class="p-badge">P${D.campus.mySlot}</span><span><b>Your parking</b><small>Zone A · assigned at 08:45</small></span></button>`
        : `<button class="park-mini" data-action="go" data-view="campus"><span class="p-badge">${icon('credit-card')}</span><span><b>RFID access</b><small>No vehicle registered</small></span></button>`}
      ${navButton({ id: 'preferences', label: 'Settings', icon: 'settings' })}
      <button class="nav-item" data-action="sign-out">${icon('log-out')}<span>Sign out</span></button>`;
    const moreOn = !BOTTOM.includes(ui.view);
    $('#bottom-nav').innerHTML = [
      `<button class="bn-item${ui.view === 'my-day' ? ' is-active' : ''}" data-action="go" data-view="my-day">${icon('house')}<span>Home</span></button>`,
      `<button class="bn-item${ui.view === 'tasks' ? ' is-active' : ''}" data-action="go" data-view="tasks">${icon('square-check-big')}<span>Tasks</span></button>`,
      `<div><button class="bn-add" data-action="new-task" aria-label="New task">${icon('plus')}</button></div>`,
      `<button class="bn-item${ui.view === 'calendar' ? ' is-active' : ''}" data-action="go" data-view="calendar">${icon('calendar-days')}<span>Calendar</span></button>`,
      `<button class="bn-item${moreOn ? ' is-active' : ''}" data-action="open-sheet">${icon('ellipsis')}<span>More</span></button>`
    ].join('');
    $('#bell-dot').hidden = !myNotes().some((n) => n.unread);
    $('#user-role').textContent = `${person(ME).role} · ${isManager() ? 'Manager' : 'Staff'}`;
    $('#theme-btn').innerHTML = icon(effectiveTheme() === 'dark' ? 'sun' : 'moon');
  }

  function paintIcons() {
    const L = window.lucide;
    if (!L || !L.createIcons) return;
    try { L.createIcons({ icons: L.icons, attrs: { 'aria-hidden': 'true' } }); } catch (e) { /* icons are decorative */ }
  }

  function render() {
    renderChrome();
    $('#view').innerHTML = (VIEWS[ui.view] || VIEWS['my-day'])();
    const app = $('#app');
    if (ui.drawer && !app.classList.contains('drawer-open')) {
      // Flush styles so the new drawer starts off-screen and slides in.
      void app.offsetWidth;
    }
    app.classList.toggle('drawer-open', ui.drawer);
    paintIcons();
    if (ui.refocus) {
      const el = document.getElementById(ui.refocus);
      if (el) el.focus();
      ui.refocus = null;
    }
  }

  function commit() { save(); render(); }

  /* ------------------------------------------------------------------ overlays */

  function toast(msg) {
    const host = $('#toast-host');
    const el = document.createElement('div');
    el.className = 'toast';
    el.innerHTML = `${icon('circle-check')}<span>${esc(msg)}</span>`;
    host.appendChild(el);
    paintIcons();
    setTimeout(() => { el.classList.add('out'); setTimeout(() => el.remove(), 320); }, 2600);
  }

  function openSheet() {
    ui.sheet = true;
    const items = navItems().filter((n) => !BOTTOM.includes(n.id)).concat([{ id: 'preferences', label: 'Settings', icon: 'settings' }]);
    $('#sheet-host').innerHTML = `<div class="sheet-back" data-action="close-sheet"></div><div class="sheet" role="dialog" aria-label="More sections"><div class="grab"></div><nav class="nav">${items.map((n) => navButton(n)).join('')}<button class="nav-item" data-action="sign-out">${icon('log-out')}<span>Sign out</span></button></nav></div>`;
    paintIcons();
  }
  function closeSheet() { ui.sheet = false; $('#sheet-host').innerHTML = ''; }

  function modalShell(title, body) {
    return `<div class="modal-back" data-action="modal-back"><div class="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title"><div class="modal-h"><h2 id="modal-title">${title}</h2><button class="icon-btn sm" data-action="close-modal" aria-label="Close">${icon('x')}</button></div>${body}</div></div>`;
  }

  function modalHtml(kind) {
    if (kind === 'task') {
      // Staff create tasks for themselves; only the manager assigns work to others.
      const staffOpts = (isManager() ? D.staff : [person(ME)]).map((s) => `<option value="${s.id}"${s.id === ME ? ' selected' : ''}>${esc(s.name)}${s.id === ME ? ' (me)' : ''}</option>`).join('');
      return modalShell('New Task', `<form class="form" data-form="new-task">
        <div class="field"><label for="nt-title">Title</label><input class="input" id="nt-title" name="title" required maxlength="120" placeholder="e.g. Prepare accreditation visit agenda" autocomplete="off"></div>
        <div class="field-row">
          <div class="field"><label for="nt-owner">Assign to</label><select class="select" id="nt-owner" name="owner">${staffOpts}</select></div>
          <div class="field"><label for="nt-priority">Priority</label><select class="select" id="nt-priority" name="priority"><option value="high">High</option><option value="medium" selected>Medium</option><option value="low">Low</option></select></div>
        </div>
        <div class="field"><label for="nt-due">Due date</label><input class="input" type="date" id="nt-due" name="due" value="${TODAY}" required></div>
        <div class="field"><label for="nt-desc">Description</label><textarea class="input textarea" id="nt-desc" name="desc" maxlength="600" placeholder="What needs to be done?"></textarea></div>
        <div class="field"><label for="nt-subs">Subtasks · one per line</label><textarea class="input textarea" id="nt-subs" name="subtasks" maxlength="1000" placeholder="Collect data&#10;Prepare draft"></textarea></div>
        <div class="modal-actions"><button type="button" class="btn btn-ghost" data-action="close-modal">Cancel</button><button class="btn btn-primary">${icon('plus')}Create Task</button></div>
      </form>`);
    }
    const catOpts = Object.keys(REQ_CAT).map((k) => `<option value="${k}">${REQ_CAT[k].label}</option>`).join('');
    return modalShell('New Service Request', `<form class="form" data-form="new-request">
      <div class="field"><label for="nr-title">What do you need?</label><input class="input" id="nr-title" name="title" required maxlength="120" placeholder="e.g. Projector not turning on" autocomplete="off"></div>
      <div class="field-row">
        <div class="field"><label for="nr-cat">Category</label><select class="select" id="nr-cat" name="cat">${catOpts}</select></div>
        <div class="field"><label for="nr-loc">Location</label><input class="input" id="nr-loc" name="location" required maxlength="80" placeholder="e.g. Room 203" autocomplete="off"></div>
      </div>
      <div class="field"><label for="nr-desc">Details</label><textarea class="input textarea" id="nr-desc" name="desc" maxlength="600" placeholder="Describe the problem"></textarea></div>
      <div class="modal-actions"><button type="button" class="btn btn-ghost" data-action="close-modal">Cancel</button><button class="btn btn-primary">${icon('send')}Submit Request</button></div>
    </form>`);
  }

  function openModal(kind) {
    ui.modal = kind;
    $('#modal-host').innerHTML = modalHtml(kind);
    paintIcons();
    const first = $('#modal-host input');
    if (first) first.focus();
  }
  function closeModal() { ui.modal = null; $('#modal-host').innerHTML = ''; }

  function closePops() {
    ['#bell-pop', '#search-pop'].forEach((sel) => { const el = $(sel); if (el) el.hidden = true; });
  }

  function renderBell() {
    const pop = $('#bell-pop');
    const items = myNotes().map((n) => `<button class="pop-item${n.unread ? ' unread' : ''}" data-action="open-note" data-id="${n.id}">${icon(n.icon, 'pi-ico')}<span>${esc(n.text)}<small>${esc(n.at)}</small></span></button>`).join('');
    pop.innerHTML = `<div class="pop-h"><b>Notifications</b><button class="link" data-action="read-all">Mark all as read</button></div>${items || '<div class="pop-empty">You are all caught up.</div>'}`;
    paintIcons();
  }

  function renderSearchPop() {
    const pop = $('#search-pop');
    const q = ui.search.trim().toLowerCase();
    if (!q || ui.view === 'tasks' || ui.view === 'requests') { pop.hidden = true; return; }
    const mgr = isManager();
    const tasks = (mgr ? state.tasks : myTasks()).filter((t) => t.title.toLowerCase().includes(q)).slice(0, 5);
    const reqs = myRequests().filter((r) => `${r.id} ${r.title}`.toLowerCase().includes(q)).slice(0, 4);
    const people = mgr ? D.staff.filter((s) => `${s.name} ${s.role} ${s.dept}`.toLowerCase().includes(q)).slice(0, 4) : [];
    let html = '';
    if (tasks.length) html += '<div class="pop-group">Tasks</div>' + tasks.map((t) => `<button class="pop-item" data-action="open-task" data-id="${t.id}">${icon('square-check-big', 'pi-ico')}<span>${esc(t.title)}<small>${esc(person(t.owner).name)} · ${STATUS[t.status].label} · ${fmtDate(t.due)}</small></span></button>`).join('');
    if (reqs.length) html += '<div class="pop-group">Service requests</div>' + reqs.map((r) => `<button class="pop-item" data-action="open-request" data-id="${r.id}">${icon((REQ_CAT[r.cat] || REQ_CAT.maintenance).icon, 'pi-ico')}<span>${esc(r.title)}<small>#${esc(r.id)} · ${REQ_STATUS[r.status].label}</small></span></button>`).join('');
    if (people.length) html += '<div class="pop-group">People</div>' + people.map((s) => `<button class="pop-item" data-action="go" data-view="team">${avatar(s.id)}<span>${esc(s.name)}<small>${esc(s.role)} · ${esc(s.dept)}</small></span></button>`).join('');
    pop.innerHTML = html || `<div class="pop-empty">Nothing matches “${esc(ui.search.trim())}”.</div>`;
    pop.hidden = false;
    paintIcons();
  }

  /* ------------------------------------------------------------------ actions */

  function go(view, keepSearch) {
    if (!VIEWS[view]) view = 'my-day';
    ui.view = view;
    ui.drawer = false;
    ui.confirmReset = false;
    ui.actConfirm = null;
    if (view !== cloud.lastPage) { cloud.lastPage = view; track('page', `Opened ${viewLabel(view)}`); }
    if (!keepSearch && ui.search) { ui.search = ''; $('#search').value = ''; }
    closePops();
    closeSheet();
    try { history.replaceState(null, '', '#' + view); } catch (e) { /* hash routing is optional */ }
    render();
    window.scrollTo(0, 0);
  }

  function selectTask(id) {
    ui.selected = id;
    if (!wide()) ui.drawer = true;
    render();
  }

  function openTask(id) {
    const t = findTask(id);
    if (!t) return;
    if (!isMine(t)) ui.taskScope = 'all';
    ui.taskFilter = 'all';
    go('tasks');
    selectTask(id);
  }

  // Every task change goes into the task's own log and the host's activity feed.
  const logTask = (t, text, type) => {
    t.log.unshift({ by: ME, at: nowLabel(), text });
    track(type || 'task', `${text} · ${t.title}`);
  };

  function setTaskStatus(t, status, note, type) {
    if (t.status === status) return;
    t.status = status;
    if (status === 'completed') t.completedOn = TODAY; else delete t.completedOn;
    logTask(t, note || `Updated task status to ${STATUS[status].label}`, type);
  }

  function decide(kind, id, approve) {
    let title;
    if (kind === 'task') {
      const t = findTask(id);
      if (!t) return;
      title = t.title;
      setTaskStatus(t, approve ? 'completed' : 'in-progress', approve ? 'Approved and closed the task' : 'Returned the task for changes', 'approval');
    } else {
      const r = state.requests.find((x) => x.id === id);
      if (!r) return;
      title = r.title;
      r.status = approve ? 'approved' : 'rejected';
      track('approval', `${approve ? 'Approved' : 'Rejected'} request #${r.id} · ${r.title}`);
    }
    state.decisions.unshift({ kind, id, title, approve, at: nowLabel() });
    toast(`${approve ? 'Approved' : kind === 'task' ? 'Returned' : 'Rejected'}: ${title}`);
    commit();
  }

  function nextRequestId() {
    const max = state.requests.reduce((m, r) => Math.max(m, parseInt(String(r.id).replace(/\D/g, ''), 10) || 0), 1000);
    return `SR-${max + 1}`;
  }

  function fmtSize(bytes) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  }
  const kindOf = (name) => (/\.(xlsx?|csv)$/i.test(name) ? 'sheet' : /\.(docx?|pdf|txt)$/i.test(name) ? 'doc' : 'other');

  function setTheme(t) {
    ui.theme = t;
    try { localStorage.setItem(THEME_KEY, t); } catch (e) { /* not remembered */ }
    applyTheme();
    render();
  }
  function applyTheme() {
    const root = document.documentElement;
    if (ui.theme === 'system') root.removeAttribute('data-theme'); else root.setAttribute('data-theme', ui.theme);
  }
  function effectiveTheme() {
    const attr = document.documentElement.getAttribute('data-theme');
    if (attr === 'light' || attr === 'dark') return attr;
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }

  function openGate() {
    campus.gate.open = true;
    campus.gate.last = fmt24(nowMin());
    campus.accessLog.unshift({ time: campus.gate.last, who: ME, method: 'Manual override', point: 'Main Gate', result: 'granted' });
    track('campus', 'Opened the main gate manually');
    toast('Main gate opened. It closes again in 8 seconds.');
    render();
    setTimeout(() => { campus.gate.open = false; if (ui.view === 'campus') render(); }, 8000);
  }

  function onClick(e) {
    if (!e.target.closest('.bell-wrap') && !e.target.closest('.search')) closePops();
    const el = e.target.closest('[data-action]');
    if (!el) return;
    const a = el.dataset.action;
    const id = el.dataset.id;
    const t = id ? findTask(id) : null;

    switch (a) {
      case 'go': go(el.dataset.view); break;
      case 'select-task': selectTask(id); break;
      case 'open-task': openTask(id); break;
      case 'open-request': ui.reqFilter = 'all'; go('requests'); break;
      case 'toggle-done':
        if (!t) break;
        if (t.status === 'completed') { setTaskStatus(t, 'in-progress', 'Reopened the task'); toast(`Reopened: ${t.title}`); }
        else { setTaskStatus(t, 'completed', 'Marked the task as complete'); toast(`Completed: ${t.title}`); }
        commit();
        break;
      case 'complete':
        if (!t) break;
        setTaskStatus(t, 'completed', 'Marked the task as complete');
        toast(`Completed: ${t.title}`);
        commit();
        break;
      case 'request-approval':
        if (!t) break;
        setTaskStatus(t, 'awaiting', 'Requested approval');
        toast(`Approval requested from ${person(t.by === ME ? 'manager' : t.by).name}`);
        commit();
        break;
      case 'toggle-sub': {
        if (!t) break;
        const s = t.subtasks[+el.dataset.i];
        if (!s) break;
        s.done = !s.done;
        if (s.done) logTask(t, `Completed subtask “${s.t}”`);
        if (s.done && t.status === 'not-started') setTaskStatus(t, 'in-progress');
        commit();
        break;
      }
      case 'close-detail':
        if (ui.drawer) { ui.drawer = false; $('#app').classList.remove('drawer-open'); }
        else { ui.selected = null; render(); }
        break;
      case 'close-drawer': ui.drawer = false; $('#app').classList.remove('drawer-open'); break;
      case 'add-file': ui.fileFor = id; $('#file-input').value = ''; $('#file-input').click(); break;
      case 'kpi': ui.taskScope = 'mine'; ui.taskFilter = el.dataset.filter; go('tasks'); break;
      case 'team-filter': ui.taskScope = 'all'; ui.taskFilter = el.dataset.filter; go('tasks'); break;
      case 'task-filter': ui.taskFilter = el.dataset.filter; render(); break;
      case 'scope': ui.taskScope = el.dataset.scope; render(); break;
      case 'req-filter': ui.reqFilter = el.dataset.filter; if (ui.view !== 'requests') go('requests'); else render(); break;
      case 'clear-search': ui.search = ''; $('#search').value = ''; render(); break;
      case 'new-task': closeSheet(); openModal('task'); break;
      case 'new-request': openModal('request'); break;
      case 'close-modal': closeModal(); break;
      case 'modal-back': if (e.target === el) closeModal(); break;
      case 'decide': decide(el.dataset.kind, id, el.dataset.ok === '1'); break;
      case 'week': ui.week = el.dataset.step === '0' ? 0 : ui.week + Number(el.dataset.step); if (el.dataset.step === '0') ui.calDay = TODAY; render(); break;
      case 'cal-day': setCalDay(el.dataset.day); break;
      case 'toggle-bell': {
        const pop = $('#bell-pop');
        const willOpen = pop.hidden;
        closePops();
        if (willOpen) { renderBell(); pop.hidden = false; }
        break;
      }
      case 'read-all': state.notifications.forEach((n) => { n.unread = false; }); save(); renderChrome(); renderBell(); $('#bell-pop').hidden = false; paintIcons(); break;
      case 'open-note': {
        const n = state.notifications.find((x) => x.id === id);
        if (!n) break;
        n.unread = false;
        save();
        closePops();
        if (n.task) openTask(n.task); else go(n.view || 'my-day');
        break;
      }
      case 'toggle-theme': setTheme(effectiveTheme() === 'dark' ? 'light' : 'dark'); break;
      case 'set-theme': setTheme(el.dataset.theme); break;
      case 'pref': ui.prefs[el.dataset.key] = !ui.prefs[el.dataset.key]; render(); break;
      case 'reset':
        if (!ui.confirmReset) { ui.confirmReset = true; render(); break; }
        state = freshState();
        accounts.forEach(seedOnboarding); // registered staff keep their onboarding checklist
        state.seeded = true;
        save();
        ui.confirmReset = false;
        ui.selected = firstOpenTaskId();
        track('settings', 'Reset the demo data');
        toast('Demo data reset');
        render();
        break;
      case 'open-sheet': openSheet(); break;
      case 'close-sheet': closeSheet(); break;
      case 'slot': campus.slot = Number(el.dataset.n); render(); break;
      case 'gate': openGate(); break;
      case 'toggle-live': campus.live = !campus.live; render(); break;
      case 'auth-tab': ui.authTab = el.dataset.tab; mountAuth(true); break;
      case 'toggle-pw': {
        const input = document.getElementById(el.dataset.for);
        if (!input) break;
        const show = input.type === 'password';
        input.type = show ? 'text' : 'password';
        el.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
        el.innerHTML = icon(show ? 'eye-off' : 'eye');
        paintIcons();
        break;
      }
      case 'sign-out': signOut(); break;
      case 'act-filter': ui.actFilter = el.dataset.filter; render(); break;
      case 'fb-recheck': recheckVerified(); break;
      case 'fb-resend': {
        const u = fbUser();
        if (!u) break;
        u.sendEmailVerification()
          .then(() => toast('Confirmation email sent. Check your inbox and spam folder.'))
          .catch(() => toast('The email could not be sent yet. Wait a minute and try again.'));
        break;
      }
      case 'user-activity': ui.actPerson = id || null; ui.actFilter = 'all'; go('activity'); break;
      case 'act-person': ui.actPerson = id && ui.actPerson !== id ? id : null; render(); break;
      case 'act-clear': {
        if (!cloud.host || !cloud.db || !id) break;
        if (ui.actConfirm !== id) { ui.actConfirm = id; render(); break; }
        ui.actConfirm = null;
        if (ui.actPerson === id) ui.actPerson = null;
        cloud.db.doc(`activity/${id}`).delete()
          .then(() => toast('Activity cleared'))
          .catch(() => toast('That activity could not be cleared. Try again.'));
        render();
        break;
      }
      default: break;
    }
  }

  function onChange(e) {
    const el = e.target;
    if (el.id === 'file-input') {
      const t = findTask(ui.fileFor);
      if (!t || !el.files || !el.files.length) return;
      Array.from(el.files).forEach((f) => t.files.push({ name: f.name, size: fmtSize(f.size), kind: kindOf(f.name) }));
      logTask(t, `Attached ${plural(el.files.length, 'file')}`);
      toast(`Attached ${plural(el.files.length, 'file')} to “${t.title}”`);
      commit();
      return;
    }
    const kind = el.dataset.change;
    if (!kind) return;
    const t = findTask(el.dataset.id);
    if (!t) return;
    if (kind === 'owner') {
      if (t.owner === ME) t.by = ME; // delegating my own task keeps it on my list
      t.owner = el.value;
      logTask(t, `Delegated the task to ${person(el.value).name}`);
      toast(`Delegated to ${person(el.value).name}`);
    } else if (kind === 'priority') {
      t.priority = el.value;
      logTask(t, `Changed priority to ${PRIORITY[el.value]}`);
    } else if (kind === 'status') {
      setTaskStatus(t, el.value);
    }
    ui.refocus = el.id;
    commit();
  }

  function onSubmit(e) {
    const f = e.target;
    const kind = f.dataset && f.dataset.form;
    if (!kind) return;
    e.preventDefault();
    if (kind === 'signin') { submitSignIn(f); return; }
    if (kind === 'register') { submitRegister(f); return; }
    const data = Object.fromEntries(new FormData(f));
    const text = String(data.text || '').trim();

    if (kind === 'comment') {
      const t = findTask(f.dataset.id);
      if (!t || !text) return;
      t.comments.push({ by: ME, at: `Today, ${nowLabel()}`, text });
      logTask(t, 'Added a comment');
      ui.refocus = 'f-comment';
      commit();
    } else if (kind === 'add-sub') {
      const t = findTask(f.dataset.id);
      if (!t || !text) return;
      t.subtasks.push({ t: text, done: false });
      logTask(t, `Added subtask “${text}”`);
      ui.refocus = 'f-sub';
      commit();
    } else if (kind === 'new-task') {
      const title = String(data.title || '').trim();
      if (!title) return;
      const t = {
        id: newId('t'), title, priority: data.priority || 'medium', owner: data.owner || ME, by: ME,
        due: data.due || TODAY, status: 'not-started', desc: String(data.desc || '').trim(),
        subtasks: String(data.subtasks || '').split('\n').map((s) => s.trim()).filter(Boolean).map((s) => ({ t: s, done: false })),
        files: [], comments: [], log: []
      };
      logTask(t, t.owner === ME ? 'Created the task' : `Created the task and assigned it to ${person(t.owner).name}`);
      state.tasks.push(t);
      closeModal();
      toast(`Task created: ${title}`);
      ui.selected = t.id;
      ui.taskFilter = 'all';
      ui.taskScope = 'mine';
      save();
      if (ui.view !== 'my-day' && ui.view !== 'tasks') go('tasks'); else render();
    } else if (kind === 'new-request') {
      const title = String(data.title || '').trim();
      if (!title) return;
      const r = { id: nextRequestId(), title, cat: data.cat || 'maintenance', location: String(data.location || '').trim(), by: ME, date: TODAY, status: 'awaiting', desc: String(data.desc || '').trim() };
      state.requests.push(r);
      track('request', `Submitted request #${r.id} · ${title} (${r.location})`);
      closeModal();
      toast(`Request #${r.id} submitted for approval`);
      ui.reqFilter = 'all';
      commit();
    } else if (kind === 'quick-add') {
      const title = String(data.title || '').trim();
      if (!title) return;
      const day = data.date || TODAY;
      if (data.type === 'meeting') {
        const start = data.time || '15:00';
        const end = fmt24(Math.min(toMin(start) + 60, 23 * 60 + 59));
        state.events.push({ id: newId('e'), date: day, start, end, title, place: 'To be confirmed', kind: 'meeting' });
        track('calendar', `Added a meeting · ${title} on ${fmtShort(day)} at ${start}`);
        toast(`Meeting added on ${fmtShort(day)} at ${start}`);
        ui.calDay = day;
      } else if (data.type === 'request') {
        const r = { id: nextRequestId(), title, cat: 'maintenance', location: 'To be confirmed', by: ME, date: TODAY, status: 'awaiting', desc: '' };
        state.requests.push(r);
        track('request', `Submitted request #${r.id} · ${title}`);
        toast(`Request #${r.id} submitted for approval`);
      } else {
        const t = { id: newId('t'), title, priority: 'medium', owner: ME, by: ME, due: day, status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] };
        logTask(t, 'Created the task');
        state.tasks.push(t);
        toast(`Task added, due ${fmtShort(day)}`);
      }
      ui.refocus = 'qa-title';
      commit();
    }
  }

  function onKey(e) {
    if (e.key !== 'Escape' || !inApp()) return;
    if (ui.modal) { closeModal(); return; }
    if (ui.sheet) { closeSheet(); return; }
    if (!$('#bell-pop').hidden || !$('#search-pop').hidden) { closePops(); return; }
    if (ui.drawer) { ui.drawer = false; $('#app').classList.remove('drawer-open'); }
  }

  /* ------------------------------------------------------------------ simulated sensors */

  // Stands in for ESP32 readings: one parking slot changes, climate and room counts drift.
  function tick() {
    if (!campus.live || document.hidden || !inApp()) return;
    const target = Math.round(TOTAL_SLOTS * 0.72);
    const occ = campus.occupied.size;
    const freeOne = occ > target || (occ === target && Math.random() < 0.5);
    const pool = [];
    for (let n = 1; n <= TOTAL_SLOTS; n++) {
      if (n !== D.campus.mySlot && campus.occupied.has(n) === freeOne) pool.push(n);
    }
    if (pool.length) {
      const n = pool[Math.floor(Math.random() * pool.length)];
      if (freeOne) campus.occupied.delete(n);
      else {
        campus.occupied.add(n);
      }
      // Log a gate entry for some arrivals so the access log stays readable.
      if (!freeOne && Math.random() < 0.25) {
        const plates = ['CA', 'GZ', 'SN', 'QL', 'MN'];
        campus.accessLog.unshift({ time: fmt24(nowMin()), who: null, label: 'Visitor vehicle', method: `Plate ${plates[n % plates.length]} ${1000 + ((n * 7919) % 9000)}`, point: 'Main Gate', result: 'granted' });
        campus.accessLog.length = Math.min(campus.accessLog.length, 12);
      }
    }
    campus.env.temp = Math.round(clamp(campus.env.temp + (Math.random() - 0.5) * 0.2, 22, 27) * 10) / 10;
    campus.env.humidity = clamp(campus.env.humidity + Math.round((Math.random() - 0.5) * 2), 36, 50);
    campus.occupancy.forEach((a) => { a.people = clamp(a.people + Math.round((Math.random() - 0.5) * 4), 0, a.capacity); });
    const typing = document.activeElement && document.activeElement.closest && document.activeElement.closest('#view form');
    if (ui.view === 'campus' && !ui.modal && !ui.sheet && !typing) render();
  }

  /* ------------------------------------------------------------------ accounts: register / sign in */

  /*
   * Demo-only accounts: kept in this browser with a salted SHA-256 password hash.
   * A real deployment must register and verify staff on the server.
   */
  const inApp = () => !!document.getElementById('app');
  const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

  /*
   * Firebase (the GitHub Pages site): real accounts and a shared database. Each
   * staff member writes only their own record and the verified manager email
   * reads everyone's; the Firestore security rules enforce this, not just this
   * page. Off on the Claude page (it has its own shared store) and wherever the
   * Firebase scripts are not loaded, where accounts stay in the browser.
   */
  const FB = (() => {
    const claudePage = !!(window.claude && typeof window.claude.use === 'function');
    if (claudePage || !window.firebase || !D.firebase) return null;
    try {
      const app = window.firebase.initializeApp(D.firebase);
      return { auth: app.auth(), store: app.firestore() };
    } catch (e) { return null; }
  })();
  const fbUser = () => (FB ? FB.auth.currentUser : null);
  const FB_ERRORS = {
    'auth/email-already-in-use': ['email', 'An account already uses this email. Sign in instead.'],
    'auth/invalid-email': ['email', 'Enter a valid email address.'],
    'auth/weak-password': ['pw', 'Use at least 8 characters, with a letter and a number.'],
    'auth/user-not-found': ['email', 'No account uses this email. Create an account first.'],
    'auth/wrong-password': ['pw', 'Email or password is incorrect.'],
    'auth/invalid-credential': ['pw', 'Email or password is incorrect.'],
    'auth/invalid-login-credentials': ['pw', 'Email or password is incorrect.'],
    'auth/too-many-requests': [null, 'Too many attempts. Wait a few minutes, then try again.'],
    'auth/network-request-failed': [null, 'No connection to the sign-in service. Check your internet and try again.']
  };

  function loadAccounts() {
    try {
      const list = JSON.parse(localStorage.getItem(ACCOUNTS_KEY) || '[]');
      return Array.isArray(list) ? list : [];
    } catch (e) { return []; }
  }
  let accounts = loadAccounts();
  // Accounts made in this browser before Firebase (they carry a local password hash) cannot sign in to Firebase.
  if (FB && accounts.some((a) => a.hash)) { accounts = accounts.filter((a) => !a.hash); saveAccounts(); }
  function saveAccounts() {
    try { localStorage.setItem(ACCOUNTS_KEY, JSON.stringify(accounts)); } catch (e) { /* account lasts for this visit only */ }
  }
  const accountToStaff = (a) => ({ id: a.id, name: a.name, role: a.role, dept: a.dept, onSite: true });
  accounts.forEach((a) => addStaff(accountToStaff(a)));
  const currentAccount = () => accounts.find((a) => a.id === ME);
  const myPlate = () => (currentAccount() || {}).plate || '';
  function upsertAccount(acct) {
    const i = accounts.findIndex((a) => a.id === acct.id);
    if (i >= 0) accounts[i] = Object.assign({}, accounts[i], acct); else accounts.push(acct);
    saveAccounts();
    addStaff(accountToStaff(acct));
  }
  // A Firebase user's profile: this browser's copy, else their saved record, else their email.
  async function accountFromCloud(u) {
    let acct = accounts.find((a) => a.id === u.uid);
    if (acct) return acct;
    let saved = null;
    try {
      const s = await FB.store.doc(`activity/${u.uid}`).get();
      saved = s.exists && s.data().account && typeof s.data().account === 'object' ? s.data().account : null;
    } catch (e) { /* fall back to the email */ }
    const email = (u.email || '').toLowerCase();
    acct = Object.assign({ name: email.split('@')[0], staffId: '', dept: '', role: 'Staff', plate: '' }, saved || {}, { id: u.uid, email });
    delete acct.kind;
    upsertAccount(acct);
    if (!state.tasks.some((t) => t.owner === acct.id)) { seedOnboarding(acct); save(); }
    return acct;
  }
  // With Firebase, Firebase itself rejects a second account with the same email.
  const emailTaken = (email) => !FB && accounts.some((a) => a.email === email);

  function loadSession() {
    try {
      const s = JSON.parse(localStorage.getItem(SESSION_KEY) || 'null');
      if (s && accounts.some((a) => a.id === s.userId)) return s.userId;
    } catch (e) { /* not signed in */ }
    return null;
  }
  function setSession(id) {
    ME = id;
    try { localStorage.setItem(SESSION_KEY, JSON.stringify({ userId: id })); } catch (e) { /* signed in for this visit only */ }
  }
  function clearSession() {
    try { localStorage.removeItem(SESSION_KEY); } catch (e) { /* nothing stored */ }
  }
  const firstOpenTaskId = () => ((myTasks().filter(isOpen).sort(byDue)[0]) || {}).id || null;

  function randomHex(bytes) {
    const a = new Uint8Array(bytes);
    crypto.getRandomValues(a);
    return Array.from(a, (b) => b.toString(16).padStart(2, '0')).join('');
  }
  async function hashPassword(pw, salt) {
    if (!(window.crypto && crypto.subtle)) throw new Error('insecure-context');
    const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(`${salt}:${pw}`));
    return Array.from(new Uint8Array(buf), (b) => b.toString(16).padStart(2, '0')).join('');
  }

  function field(o) {
    const control = o.control || `<input class="input" id="${o.id}" name="${o.name}" type="${o.type || 'text'}"${o.auto ? ` autocomplete="${o.auto}"` : ''}${o.placeholder ? ` placeholder="${esc(o.placeholder)}"` : ''} aria-describedby="${o.id}-msg"${o.attrs || ''}>`;
    return `<div class="field"><label for="${o.id}">${o.label}${o.optional ? '<span class="opt">Optional</span>' : ''}</label>${control}<p class="field-msg" id="${o.id}-msg" data-hint="${esc(o.hint || '')}">${esc(o.hint || '')}</p></div>`;
  }
  function pwField(id, name, label, auto, hint) {
    return field({ id, label, hint, control: `<div class="pw-wrap"><input class="input" id="${id}" name="${name}" type="password" autocomplete="${auto}" aria-describedby="${id}-msg"><button type="button" class="icon-btn sm pw-toggle" data-action="toggle-pw" data-for="${id}" aria-label="Show password">${icon('eye')}</button></div>` });
  }

  function signInForm() {
    return `<div class="auth-head"><h2>Welcome back</h2><p class="sub">Sign in with the email you registered.</p></div>
      <form class="form" data-form="signin" novalidate>
        <p class="form-error" id="signin-error" role="alert" hidden></p>
        ${field({ id: 'si-email', name: 'email', label: 'Work email', type: 'email', auto: 'email', placeholder: 'name@eng.asu.edu.eg' })}
        ${pwField('si-pw', 'password', 'Password', 'current-password')}
        <button class="btn btn-primary btn-block">${icon('log-in')}Sign in</button>
      </form>
      <p class="auth-switch">New staff member? <button class="link" data-action="auth-tab" data-tab="register">Create an account</button></p>`;
  }

  function registerForm() {
    const domains = D.auth.allowedDomains;
    const emailHint = domains.length ? `Use your faculty email ending in @${domains.join(' or @')}.` : 'Your work email. You will sign in with it.';
    const depts = DEPARTMENTS.map((d) => `<option>${esc(d)}</option>`).join('');
    return `<div class="auth-head"><h2>Create your staff account</h2><p class="sub">Register once with your email. Your staff ID and plate let the gate recognise you on arrival.</p></div>
      <form class="form" data-form="register" novalidate>
        <p class="form-error" id="register-error" role="alert" hidden></p>
        ${field({ id: 'rg-name', name: 'name', label: 'Full name', auto: 'name', placeholder: 'e.g. Mona Adel' })}
        ${field({ id: 'rg-email', name: 'email', label: 'Work email', type: 'email', auto: 'email', placeholder: 'name@eng.asu.edu.eg', hint: emailHint })}
        <div class="field-row">
          ${field({ id: 'rg-staff', name: 'staffId', label: 'Staff ID', placeholder: 'e.g. ENG-2041', hint: 'As printed on your staff card.', attrs: ' autocapitalize="characters"' })}
          ${field({ id: 'rg-dept', label: 'Department', control: `<select class="select" id="rg-dept" name="dept" aria-describedby="rg-dept-msg">${depts}</select>` })}
        </div>
        <div class="field-row">
          ${field({ id: 'rg-role', name: 'role', label: 'Job title', auto: 'organization-title', placeholder: 'e.g. Lab Engineer' })}
          ${field({ id: 'rg-plate', name: 'plate', label: 'Vehicle plate', optional: true, placeholder: 'e.g. CA 1234', hint: 'For automatic gate entry.', attrs: ' autocapitalize="characters"' })}
        </div>
        ${pwField('rg-pw', 'password', 'Password', 'new-password', 'At least 8 characters, with a letter and a number.')}
        ${pwField('rg-pw2', 'confirm', 'Confirm password', 'new-password')}
        <div class="field"><label class="check-row" for="rg-consent"><input type="checkbox" id="rg-consent" name="consent" aria-describedby="rg-consent-msg"><span>I agree that my staff ID and vehicle plate are used for gate and building access.</span></label><p class="field-msg" id="rg-consent-msg" data-hint=""></p></div>
        <button class="btn btn-primary btn-block">${icon('user-plus')}Create account</button>
      </form>
      <p class="auth-switch">Already registered? <button class="link" data-action="auth-tab" data-tab="signin">Sign in</button></p>`;
  }

  function authHtml() {
    const reg = ui.authTab === 'register';
    return `
    <div class="auth">
      <section class="auth-brand">
        <div class="brand"><span class="brand-mark">${icon('building-2')}</span><span class="brand-text"><b>ASU FOE</b><small>Smart Staff Experience</small></span></div>
        <div class="auth-hero">
          <span class="auth-kicker">Smart City project · ASU Faculty of Engineering</span>
          <h1>Smart Staff <em>Experience</em></h1>
          <p>Arrive, get verified at the gate, find your parking space and start a day that is already organised.</p>
        </div>
        <ol class="auth-steps">
          <li><span>1</span><div><b>Staff verified</b><small>Access granted at the main gate</small></div></li>
          <li><span>2</span><div><b>Parking space assigned</b><small>Navigate to your space</small></div></li>
          <li><span>3</span><div><b>Office arrival</b><small>Today's tasks are ready</small></div></li>
        </ol>
      </section>
      <main class="auth-main">
        <div class="auth-card">
          <div class="seg auth-tabs" role="tablist" aria-label="Account">
            <button role="tab" aria-selected="${!reg}" class="${reg ? '' : 'is-on'}" data-action="auth-tab" data-tab="signin">Sign in</button>
            <button role="tab" aria-selected="${reg}" class="${reg ? 'is-on' : ''}" data-action="auth-tab" data-tab="register">Create account</button>
          </div>
          ${reg ? registerForm() : signInForm()}
          <p class="auth-note">${icon('info')}<span>Concept prototype, not an official ASU system. ${FB ? 'Choose a new password for this site, not your university password.' : 'Accounts are saved only in this browser, so don’t use your real university password.'}<span id="host-note">${cloud.db || FB ? HOST_NOTE : ''}</span></span></p>
        </div>
      </main>
    </div>`;
  }

  function mountAuth(focus) {
    $('#root').innerHTML = authHtml();
    paintIcons();
    window.scrollTo(0, 0);
    if (focus) { const first = $('.auth-card form input'); if (first) first.focus(); }
  }

  // Shows each error under its field (or the field's hint when it is fine). Returns true when there are none.
  function showErrors(form, errors, ids) {
    form.querySelectorAll('.field-msg').forEach((m) => { m.textContent = m.dataset.hint || ''; m.classList.remove('is-err'); });
    form.querySelectorAll('[aria-invalid]').forEach((el) => el.removeAttribute('aria-invalid'));
    let first = null;
    Object.keys(errors).forEach((k) => {
      const input = document.getElementById(ids[k]);
      const msg = document.getElementById(`${ids[k]}-msg`);
      if (input) { input.setAttribute('aria-invalid', 'true'); first = first || input; }
      if (msg) { msg.textContent = errors[k]; msg.classList.add('is-err'); }
    });
    if (first) first.focus();
    return !first;
  }
  function formError(form, text) {
    const box = form.querySelector('.form-error');
    if (box) { box.textContent = text; box.hidden = false; }
  }

  const REG_IDS = { name: 'rg-name', email: 'rg-email', staffId: 'rg-staff', role: 'rg-role', plate: 'rg-plate', pw: 'rg-pw', pw2: 'rg-pw2', consent: 'rg-consent' };

  function validateRegister(v) {
    const e = {};
    const domains = D.auth.allowedDomains;
    if (v.name.length < 3 || !v.name.includes(' ')) e.name = 'Enter your first and last name.';
    if (!EMAIL_RE.test(v.email)) e.email = 'Enter a valid email address, like name@eng.asu.edu.eg.';
    else if (domains.length && !domains.some((d) => v.email.endsWith(`@${d}`))) e.email = `Use your faculty email ending in @${domains.join(' or @')}.`;
    else if (emailTaken(v.email)) e.email = 'An account already uses this email. Sign in instead.';
    if (!/^[A-Z0-9-]{4,12}$/.test(v.staffId)) e.staffId = 'Use 4–12 letters, numbers or dashes, as printed on your card.';
    else if (!FB && accounts.some((a) => a.staffId === v.staffId)) e.staffId = 'This staff ID is already registered.';
    if (!v.role) e.role = 'Enter your job title.';
    if (v.plate && !/^(?=.*\d)[A-Z0-9\u0600-\u06FF -]{3,12}$/.test(v.plate)) e.plate = 'Enter letters and numbers, like CA 1234.';
    if (v.pw.length < 8 || !/[A-Za-z]/.test(v.pw) || !/\d/.test(v.pw)) e.pw = 'Use at least 8 characters, with a letter and a number.';
    if (!v.pw2) e.pw2 = 'Enter the password again.';
    else if (v.pw2 !== v.pw) e.pw2 = 'Passwords do not match.';
    if (!v.consent) e.consent = 'Tick this box so the gate can recognise your card and vehicle.';
    return e;
  }

  // New staff start with a short onboarding checklist from their department head.
  function seedOnboarding(a) {
    const log = [{ by: 'manager', at: nowLabel(), text: `Assigned to ${a.name}` }];
    const list = [
      { title: 'Collect your RFID staff card', priority: 'high', due: addDays(TODAY, 1), status: 'not-started',
        desc: 'Visit IT Services to register your staff card on the RC522 gate and building readers.',
        subtasks: ['Bring your national ID', 'Card registered on the gate readers', 'Test the building entrance'] },
      a.plate
        ? { title: 'Register your vehicle plate for gate access', priority: 'medium', due: TODAY, status: 'completed', completedOn: TODAY,
          desc: `Plate ${a.plate} was added when you signed up. The ESP32-CAM at the main gate will recognise it.`, subtasks: [] }
        : { title: 'Register your vehicle plate for gate access', priority: 'medium', due: addDays(TODAY, 2), status: 'not-started',
          desc: 'Send your plate number to Campus Security so the entrance camera can open the gate for you. Skip this if you arrive on foot.', subtasks: [] },
      { title: 'Read the department handbook', priority: 'low', due: addDays(TODAY, 4), status: 'not-started',
        desc: 'Working hours, service requests and how approvals work in the Smart Staff system.',
        subtasks: ['Working hours and leave', 'Raising a service request', 'Task approval workflow'] }
    ];
    const ids = list.map((t) => {
      const id = newId('t');
      state.tasks.push(Object.assign({ id, owner: a.id, by: 'manager', files: [], comments: [], log: clone(log) }, t, { subtasks: t.subtasks.map((s) => ({ t: s, done: false })) }));
      return id;
    });
    state.events.push({ id: newId('e'), date: TODAY, start: '13:00', end: '13:30', title: `Welcome meeting · ${a.name}`, place: 'Dean\'s Office', kind: 'meeting' });
    state.notifications.unshift({ id: newId('n'), icon: 'user-check', text: `Welcome, ${a.name.split(' ')[0]}! Your onboarding tasks are ready.`, at: nowLabel(), unread: true, task: ids[0] });
    return ids[0];
  }

  function signIn(id, selectId, how) {
    setSession(id);
    if (FB) resetCloud(); // a different person may now be signed in
    cloud.signedIn = true;
    cloud.account = accountSummary();
    cloud.lastPage = 'my-day';
    track(how && how.startsWith('Created') ? 'account' : 'signin', how || 'Signed in');
    Object.assign(ui, { view: 'my-day', drawer: false, taskScope: 'mine', taskFilter: 'all', search: '', confirmReset: false });
    ui.selected = selectId || firstOpenTaskId();
    mountApp();
    if (FB) initCloud();
  }

  async function signOut() {
    track('signout', 'Signed out');
    cloud.signedIn = false;
    if (FB) {
      clearTimeout(cloud.timer);
      await flush(); // save the sign-out before the session ends
      FB.auth.signOut().catch(() => { /* already signed out */ });
      resetCloud();
    }
    clearSession();
    closeModal();
    closeSheet();
    ME = null;
    Object.assign(ui, { view: 'my-day', drawer: false, search: '', authTab: 'signin' });
    mountAuth();
  }

  async function submitRegister(f) {
    const fd = new FormData(f);
    const g = (n) => String(fd.get(n) || '').trim();
    const v = {
      name: g('name').replace(/\s+/g, ' '), email: g('email').toLowerCase(), staffId: g('staffId').toUpperCase(),
      dept: g('dept'), role: g('role'), plate: g('plate').toUpperCase().replace(/\s+/g, ' '),
      pw: String(fd.get('password') || ''), pw2: String(fd.get('confirm') || ''), consent: !!fd.get('consent')
    };
    if (!showErrors(f, validateRegister(v), REG_IDS)) return;
    const btn = f.querySelector('.btn-primary');
    btn.disabled = true;
    try {
      const profile = { name: v.name, email: v.email, staffId: v.staffId, dept: v.dept, role: v.role, plate: v.plate, created: new Date().toISOString() };
      let acct;
      if (FB) {
        const cred = await FB.auth.createUserWithEmailAndPassword(v.email, v.pw);
        acct = Object.assign({ id: cred.user.uid }, profile);
        // The manager email must be confirmed before it can read other staff's records.
        if (D.auth.managerEmails.includes(v.email)) cred.user.sendEmailVerification().catch(() => { /* resend from My Day */ });
      } else {
        const salt = randomHex(16);
        acct = Object.assign({ id: `u${Date.now().toString(36)}${randomHex(2)}`, salt, hash: await hashPassword(v.pw, salt) }, profile);
      }
      upsertAccount(acct);
      const first = seedOnboarding(acct);
      save();
      signIn(acct.id, first, `Created an account · ${acct.email} · ${acct.dept}`);
      toast(`Account created. Welcome, ${firstName(acct.id)}!`);
    } catch (e) {
      btn.disabled = false;
      const known = e && FB_ERRORS[e.code];
      if (known && known[0]) { showErrors(f, { [known[0]]: known[1] }, REG_IDS); return; }
      formError(f, known ? known[1] : e && e.message === 'insecure-context'
        ? 'Registration needs a secure address. Open the site over https or from localhost.'
        : 'The account could not be created. Try again.');
    }
  }

  async function submitSignIn(f) {
    const fd = new FormData(f);
    const email = String(fd.get('email') || '').trim().toLowerCase();
    const pw = String(fd.get('password') || '');
    const ids = { email: 'si-email', pw: 'si-pw' };
    const errors = {};
    if (!EMAIL_RE.test(email)) errors.email = 'Enter the email you registered with.';
    if (!pw) errors.pw = 'Enter your password.';
    if (!showErrors(f, errors, ids)) return;
    if (FB) {
      const btn = f.querySelector('.btn-primary');
      btn.disabled = true;
      try {
        const cred = await FB.auth.signInWithEmailAndPassword(email, pw);
        const acct = await accountFromCloud(cred.user);
        signIn(acct.id, null, `Signed in · ${acct.email}`);
        toast(`Signed in as ${acct.name}`);
      } catch (e) {
        btn.disabled = false;
        const known = (e && FB_ERRORS[e.code]) || [null, 'Sign-in failed. Try again.'];
        if (known[0]) showErrors(f, { [known[0]]: known[1] }, ids); else formError(f, known[1]);
      }
      return;
    }
    const acct = accounts.find((a) => a.email === email);
    if (!acct) {
      showErrors(f, { email: 'No account uses this email. Create an account first.' }, ids);
      return;
    }
    let hash;
    try { hash = await hashPassword(pw, acct.salt); } catch (e) {
      formError(f, 'Sign in needs a secure address. Open the site over https or from localhost.');
      return;
    }
    if (hash !== acct.hash) { showErrors(f, { pw: 'Incorrect password. Try again.' }, ids); return; }
    signIn(acct.id, null, `Signed in · ${acct.email}`);
    toast(`Signed in as ${acct.name}`);
  }

  /* ------------------------------------------------------------------ host activity log */

  /*
   * On the published Claude page, each signed-in viewer's actions are saved to
   * their own document, activity/<their id>, which only the page owner (the
   * host) can read. Anywhere else (the zip, a local server) `window.claude`
   * is absent and nothing is recorded.
   */
  const LOG_LIMIT = 150; // newest events kept per person
  const cloud = {
    ready: false, db: null, user: null, uid: null, host: false, off: false,
    loaded: false, mine: null, pending: [], writing: false, timer: null, retries: 0,
    signedIn: false, account: null, lastPage: null, docs: [], profiles: {}, unsub: null
  };

  /*
   * Roles. A dashboard account whose email is in D.auth.managerEmails is the
   * manager; everyone else is staff. Dashboard accounts live in each browser,
   * so on the published page the manager must also be the page owner: only
   * the owner's Claude account can read other staff's data.
   */
  const onPlatform = !!(window.claude && typeof window.claude.use === 'function');
  const HOST_NOTE = ' Other users cannot see your data. The manager of this page can see your statistics and activity.';
  const managerEmail = () => { const a = currentAccount(); return !!a && D.auth.managerEmails.includes(a.email); };
  // On Firebase the manager email must also be confirmed, as the database rules require.
  const managerUnverified = () => !!FB && managerEmail() && !(fbUser() && fbUser().emailVerified);
  function isManager() {
    if (!managerEmail()) return false;
    if (onPlatform) return cloud.host;
    if (FB) return !managerUnverified();
    return true;
  }

  function verifyCard() {
    const a = currentAccount() || {};
    return `<section class="card verify-card">
      <span class="li-ico tone-aw">${icon('mail')}</span>
      <div><b>Confirm your email to open the manager pages</b>
        <span>A confirmation link was sent to ${esc(a.email)}. Open it (check spam too), then come back and click “I've confirmed”.</span></div>
      <div class="acts"><button class="btn btn-primary sm" data-action="fb-recheck">${icon('check')}I've confirmed</button><button class="btn btn-ghost sm" data-action="fb-resend">Send the link again</button></div>
    </section>`;
  }

  async function recheckVerified() {
    const u = fbUser();
    if (!u) return;
    try { await u.reload(); await u.getIdToken(true); } catch (e) { /* keep the current state */ }
    if (!(fbUser() && fbUser().emailVerified)) { toast('Not confirmed yet. Open the link in the email first.'); return; }
    toast('Email confirmed. Manager pages unlocked.');
    resetCloud();
    Object.assign(cloud, { signedIn: true, account: accountSummary() });
    render();
    initCloud();
  }
  const managerOnly = () => `<div class="stack"><section class="card empty">${icon('lock')}<b>Managers only</b><span>This page shows other people's work, so only the manager can open it. Your own tasks, requests and report are in the menu.</span></section></div>`;

  // The signed-in user's own numbers, sent with their activity for the manager's statistics.
  function myStats() {
    const own = state.tasks.filter((t) => t.owner === ME);
    const by = (s) => own.filter((t) => t.status === s).length;
    const done = by('completed');
    return {
      tasks: own.length,
      notStarted: by('not-started'),
      inProgress: by('in-progress'),
      awaiting: by('awaiting'),
      completed: done,
      overdue: own.filter(isOverdue).length,
      completion: own.length ? Math.round((100 * done) / own.length) : 0,
      requests: state.requests.filter((r) => r.by === ME).length,
      comments: state.tasks.reduce((n, t) => n + t.comments.filter((c) => c.by === ME).length, 0)
    };
  }

  function accountSummary() {
    const a = currentAccount();
    return a ? { kind: 'registered', name: a.name, email: a.email, staffId: a.staffId, dept: a.dept, role: a.role } : null;
  }

  function track(type, text) {
    if (cloud.off) return;
    cloud.pending.push({ at: new Date().toISOString(), type, text: String(text).slice(0, 240), as: cloud.account ? cloud.account.name : '' });
    if (cloud.pending.length > 60) cloud.pending.splice(0, cloud.pending.length - 60);
    flushSoon();
  }

  function flushSoon() {
    if (!cloud.loaded || cloud.off || cloud.timer) return;
    cloud.timer = setTimeout(flush, 1500);
  }

  // One write at a time to this viewer's own document, batching what happened since the last one.
  async function flush() {
    cloud.timer = null;
    if (cloud.writing || !cloud.pending.length || !cloud.db || cloud.off) return;
    cloud.writing = true;
    const batch = cloud.pending.splice(0);
    const prev = cloud.mine || {};
    const prevCounts = prev.counts && typeof prev.counts === 'object' ? prev.counts : {};
    const next = {
      firstSeen: prev.firstSeen || batch[0].at,
      lastSeen: batch[batch.length - 1].at,
      signedIn: cloud.signedIn,
      account: cloud.account || prev.account || null,
      stats: cloud.signedIn ? myStats() : (prev.stats || null),
      counts: {
        signIns: (prevCounts.signIns || 0) + batch.filter((e) => e.type === 'signin' || e.type === 'account').length,
        actions: (prevCounts.actions || 0) + batch.filter((e) => e.type !== 'page').length
      },
      events: batch.slice().reverse().concat(Array.isArray(prev.events) ? prev.events : []).slice(0, LOG_LIMIT)
    };
    try {
      await cloud.db.doc(`activity/${cloud.uid}`).set(next);
      cloud.mine = next;
      cloud.retries = 0;
    } catch (e) {
      const code = e && e.code;
      if (code === 'unavailable' && cloud.retries < 3) {
        cloud.retries += 1;
        cloud.pending = batch.concat(cloud.pending);
      } else {
        cloud.off = true; // no write access here (e.g. a view-only visitor): stop recording quietly
      }
    }
    cloud.writing = false;
    if (cloud.pending.length) flushSoon();
  }

  function subscribeHost() {
    cloud.unsub = cloud.db.collection('activity').onSnapshot((snap) => {
      cloud.docs = snap.docs.filter((d) => d.exists).map((d) => Object.assign({ id: d.id }, d.data()));
      cloud.docs.forEach((d) => { if (d.account && typeof d.account === 'object') knowPerson(d.id, d.account); });
      resolveProfiles();
      if (inApp() && ['activity', 'users', 'team'].includes(ui.view)) render();
    }, () => { /* the live feed stopped; the rest of the page keeps working */ });
  }

  // Forget the signed-in person's cloud state (Firebase: someone else may sign in next).
  function resetCloud() {
    if (cloud.unsub) { try { cloud.unsub(); } catch (e) { /* already stopped */ } }
    clearTimeout(cloud.timer);
    Object.assign(cloud, {
      ready: false, db: null, user: null, uid: null, host: false, off: false, loaded: false, mine: null,
      pending: [], writing: false, timer: null, retries: 0, docs: [], profiles: {}, unsub: null
    });
  }

  // The same small db/user surface the Claude page offers, backed by Firestore and Firebase Auth.
  function firebaseCloud(u) {
    const wrap = (s) => ({ id: s.id, exists: s.exists, data: () => s.data() });
    const db = {
      doc: (path) => {
        const ref = FB.store.doc(path);
        return { get: async () => wrap(await ref.get()), set: (d) => ref.set(d), delete: () => ref.delete() };
      },
      collection: (path) => ({
        onSnapshot: (next, err) => FB.store.collection(path).onSnapshot((q) => next({ docs: q.docs.map(wrap) }), err)
      })
    };
    const user = {
      id: async () => u.uid,
      isOwner: async () => !!u.emailVerified && D.auth.managerEmails.includes((u.email || '').toLowerCase()),
      // Names come from each person's own record; there are no profile photos.
      profiles: async (ids) => Object.fromEntries([].concat(ids).map((id) => {
        const d = cloud.docs.find((x) => x.id === id);
        return [id, { id, name: (d && d.account && d.account.name) || '', avatarUrl: '' }];
      }))
    };
    return { db, user };
  }

  // Names and photos come from Claude accounts, resolved fresh each time (never stored).
  async function resolveProfiles() {
    const ids = cloud.docs.map((d) => d.id);
    if (!cloud.user || !ids.length) return;
    const ps = await cloud.user.profiles(ids);
    const changed = ids.some((id) => !cloud.profiles[id] || cloud.profiles[id].name !== ps[id].name || cloud.profiles[id].avatarUrl !== ps[id].avatarUrl);
    cloud.profiles = ps;
    if (changed && inApp() && (ui.view === 'activity' || ui.view === 'users')) render();
  }

  async function initCloud() {
    try {
      let db;
      let user;
      if (FB) {
        if (!fbUser()) { cloud.off = true; return; }
        ({ db, user } = firebaseCloud(fbUser()));
      } else {
        if (!window.claude || typeof window.claude.use !== 'function') { cloud.off = true; return; }
        [db, user] = await Promise.all([window.claude.use('db'), window.claude.use('user')]);
      }
      if (!db || !user) { cloud.off = true; return; }
      const uid = await user.id();
      if (!uid) { cloud.off = true; return; }
      Object.assign(cloud, { db, user, uid, host: await user.isOwner() });
      if (cloud.host) subscribeHost();
      const snap = await db.doc(`activity/${uid}`).get();
      cloud.mine = snap.exists ? clone(snap.data()) : null;
      cloud.loaded = true;
      flushSoon();
    } catch (e) {
      cloud.off = true;
    } finally {
      cloud.ready = true;
      if (cloud.off) cloud.pending = [];
      const note = $('#host-note');
      if (note && cloud.db) note.textContent = HOST_NOTE;
      if (inApp()) render();
    }
  }

  const ACT_TYPES = {
    signin: { icon: 'log-in' }, signout: { icon: 'log-out' }, account: { icon: 'user-plus' }, visit: { icon: 'log-in' }, page: { icon: 'eye' },
    task: { icon: 'square-check-big' }, approval: { icon: 'badge-check' }, request: { icon: 'wrench' },
    calendar: { icon: 'calendar-days' }, campus: { icon: 'door-open' }, settings: { icon: 'settings' }
  };
  const ACT_FILTERS = [
    { id: 'all', label: 'All', test: () => true },
    { id: 'signin', label: 'Sign-ins', test: (e) => ['signin', 'signout', 'account', 'visit'].includes(e.type) },
    { id: 'task', label: 'Tasks', test: (e) => e.type === 'task' },
    { id: 'approval', label: 'Approvals & requests', test: (e) => e.type === 'approval' || e.type === 'request' },
    { id: 'page', label: 'Pages visited', test: (e) => e.type === 'page' }
  ];

  function relTime(iso) {
    const t = Date.parse(iso);
    if (!t) return '';
    const s = Math.round((Date.now() - t) / 1000);
    if (s < 45) return 'just now';
    if (s < 3600) return `${Math.round(s / 60)} min ago`;
    if (s < 86400) return `${Math.round(s / 3600)} h ago`;
    const d = new Date(t);
    return `${d.getDate()} ${MONTHS[d.getMonth()]}, ${pad(d.getHours())}:${pad(d.getMinutes())}`;
  }

  // The manager pages read everyone's log, so they open only for the page owner on the published page.
  function managerGate() {
    if (!onPlatform && !FB) return `<div class="stack"><section class="card empty">${icon('cloud')}<b>Needs the shared database</b><span>User statistics and activity come from a shared database. This copy runs only in your browser.</span></section></div>`;
    if (managerUnverified()) return `<div class="stack">${verifyCard()}</div>`;
    if (!cloud.ready) return `<div class="stack"><section class="card empty">${icon('loader')}<b>Checking manager access…</b></section></div>`;
    if (!isManager()) return managerOnly();
    return '';
  }
  const who = (id) => (cloud.profiles[id] && cloud.profiles[id].name) || (STAFF[id] && STAFF[id].name) || 'Someone';
  function face(id, cls) {
    const p = cloud.profiles[id];
    if (p && p.avatarUrl) return `<img class="av-img${cls ? ' ' + cls : ''}" src="${esc(p.avatarUrl)}" alt="">`;
    if (STAFF[id]) return avatar(id, cls);
    return `<span class="av${cls ? ' ' + cls : ''}" style="--h:210" aria-hidden="true">?</span>`;
  }
  const byLastSeen = () => cloud.docs.slice().sort((a, b) => String(b.lastSeen || '').localeCompare(String(a.lastSeen || '')));

  function viewActivity() {
    const gate = managerGate();
    if (gate) return gate;
    const people = byLastSeen();
    const all = [];
    people.forEach((p) => (Array.isArray(p.events) ? p.events : []).forEach((e) => {
      if (e && typeof e === 'object') all.push({ uid: p.id, at: String(e.at || ''), type: String(e.type || ''), text: String(e.text || ''), as: String(e.as || '') });
    }));
    all.sort((a, b) => b.at.localeCompare(a.at));
    const f = ACT_FILTERS.find((x) => x.id === ui.actFilter) || ACT_FILTERS[0];
    const scoped = ui.actPerson ? all.filter((e) => e.uid === ui.actPerson) : all;
    const feed = scoped.filter(f.test).slice(0, 150);
    const dayAgo = Date.now() - 864e5;
    const active = people.filter((p) => Date.parse(p.lastSeen || '') > dayAgo).length;
    const registered = people.filter((p) => p.account && p.account.kind === 'registered').length;

    const accountLine = (p) => {
      const a = p.account;
      if (!a || typeof a !== 'object') return 'Not signed in to the dashboard yet';
      if (a.kind === 'demo') return 'Demo account';
      return [a.name, a.email, a.staffId && `Staff ID ${a.staffId}`, a.dept].filter(Boolean).map((x) => esc(x)).join(' · ');
    };
    const feedRows = feed.map((e) => `<div class="feed-row">${face(e.uid)}
        <div class="feed-main"><div class="feed-who"><b>${esc(who(e.uid))}</b>${e.uid === cloud.uid ? '<span class="pill ns">You</span>' : ''}${e.as ? `<span class="feed-as">as ${esc(e.as)}</span>` : ''}</div>
        <p>${icon((ACT_TYPES[e.type] || ACT_TYPES.page).icon)}<span>${esc(e.text)}</span></p></div>
        <time title="${esc(e.at ? new Date(e.at).toLocaleString() : '')}">${relTime(e.at)}</time></div>`).join('');
    const peopleRows = people.map((p) => {
      const n = Array.isArray(p.events) ? p.events.length : 0;
      const confirming = ui.actConfirm === p.id;
      return `<div class="person${ui.actPerson === p.id ? ' is-sel' : ''}">
        <button class="person-main" data-action="act-person" data-id="${esc(p.id)}" aria-pressed="${ui.actPerson === p.id}">${face(p.id, 'av-md')}
          <span><b>${esc(who(p.id))}${p.id === cloud.uid ? ' (you)' : ''}</b><small>${accountLine(p)}</small><small>${p.signedIn === false ? 'Signed out · ' : ''}Last active ${relTime(p.lastSeen)} · ${plural(n, 'action')}</small></span></button>
        <button class="btn ${confirming ? 'btn-danger' : 'btn-ghost'} sm" data-action="act-clear" data-id="${esc(p.id)}">${confirming ? 'Confirm clear' : 'Clear'}</button>
      </div>`;
    }).join('');

    return `
    <div class="stack">
      <div class="band"><h1>Activity<span>· only you, the manager, can see this page</span></h1><span class="live"><i></i>Live</span></div>
      <div class="kpis">
        ${kpi({ icon: 'users', tone: 'ip', n: people.length, label: 'People recorded' })}
        ${kpi({ icon: 'activity', tone: 'teal', n: active, label: 'Active in the last 24 hours' })}
        ${kpi({ icon: 'user-plus', tone: 'ok', n: registered, label: 'Signed up with an account' })}
        ${kpi({ icon: 'list-checks', tone: 'violet', n: all.length, label: 'Actions recorded' })}
      </div>
      ${people.length ? `
      <div class="act-grid">
        <section class="card">
          <div class="card-h"><h2 class="card-title">${ui.actPerson ? `Activity · ${esc(who(ui.actPerson))}` : 'Everyone’s activity'}</h2>${ui.actPerson ? '<button class="link" data-action="act-person" data-id="">Show everyone</button>' : ''}</div>
          <div class="card-body"><div class="chips" role="group" aria-label="Filter activity">${ACT_FILTERS.map((x) => `<button class="chip${x.id === f.id ? ' is-on' : ''}" data-action="act-filter" data-filter="${x.id}" aria-pressed="${x.id === f.id}">${x.label} <b>${scoped.filter(x.test).length}</b></button>`).join('')}</div></div>
          <div class="feed">${feedRows || `<div class="empty">${icon('inbox')}<p>Nothing matches this filter yet.</p></div>`}</div>
        </section>
        <section class="card">
          <div class="card-h"><h2 class="card-title">People</h2><span class="muted">Newest first</span></div>
          <div class="list">${peopleRows}</div>
        </section>
      </div>` : `<section class="card empty">${icon('eye')}<b>No activity yet</b><span>When someone with access to this page signs in or does something, it appears here within a few seconds.</span></section>`}
    </div>`;
  }

  // Every user's numbers, as their own pages last reported them (stored data is untrusted: coerce it).
  function viewUsers() {
    const gate = managerGate();
    if (gate) return gate;
    const num = (v) => (Number.isFinite(Number(v)) ? Math.max(0, Math.round(Number(v))) : 0);
    const users = byLastSeen().map((p) => {
      const s = p.stats && typeof p.stats === 'object' ? p.stats : {};
      const c = p.counts && typeof p.counts === 'object' ? p.counts : {};
      const a = p.account && typeof p.account === 'object' ? p.account : {};
      return {
        id: p.id, lastSeen: String(p.lastSeen || ''), signedIn: p.signedIn !== false,
        name: String(a.name || ''), email: String(a.email || ''),
        dept: a.kind === 'demo' ? 'Demo account' : String(a.dept || 'Not signed in yet'),
        tasks: num(s.tasks), notStarted: num(s.notStarted), inProgress: num(s.inProgress), awaiting: num(s.awaiting),
        completed: num(s.completed), overdue: num(s.overdue), completion: Math.min(100, num(s.completion)),
        requests: num(s.requests), signIns: num(c.signIns), actions: num(c.actions)
      };
    });
    if (!users.length) {
      return `<div class="stack">
        <div class="band"><h1>Users<span>· statistics for everyone using this page</span></h1><span class="tag">Private to you</span></div>
        <section class="card empty">${icon('chart-pie')}<b>No users yet</b><span>Each person's statistics appear here after they sign in to the dashboard on this page.</span></section>
      </div>`;
    }
    const sum = (k) => users.reduce((n, u) => n + u[k], 0);
    const active = users.filter((u) => Date.parse(u.lastSeen) > Date.now() - 864e5).length;
    const withTasks = users.filter((u) => u.tasks);
    const avg = withTasks.length ? Math.round(withTasks.reduce((n, u) => n + u.completion, 0) / withTasks.length) : 0;
    const segs = [
      { label: 'Not Started', value: sum('notStarted'), color: 'var(--ns)' },
      { label: 'In Progress', value: sum('inProgress'), color: 'var(--ip)' },
      { label: 'Awaiting Approval', value: sum('awaiting'), color: 'var(--aw)' },
      { label: 'Completed', value: sum('completed'), color: 'var(--ok)' }
    ];
    const depts = {};
    users.forEach((u) => {
      const d = depts[u.dept] || (depts[u.dept] = { name: u.dept, users: 0, tasks: 0, completed: 0, overdue: 0 });
      d.users += 1; d.tasks += u.tasks; d.completed += u.completed; d.overdue += u.overdue;
    });
    const deptRows = Object.values(depts).sort((a, b) => b.users - a.users || a.name.localeCompare(b.name));
    const ranked = withTasks.slice().sort((a, b) => b.completion - a.completion || b.completed - a.completed);
    const label = (u) => u.name || who(u.id);
    const sub = (u) => [u.name && who(u.id) !== 'Someone' && who(u.id) !== u.name ? `Claude: ${who(u.id)}` : '', u.email].filter(Boolean).map(esc).join(' · ');
    const pctBar = (p) => `<div class="prog"><span class="prog-n">${p}%</span><span class="bar"><i style="width:${p}%"${p === 100 ? ' class="ok"' : ''}></i></span></div>`;

    return `
    <div class="stack">
      <div class="band"><h1>Users<span>· statistics for everyone using this page</span></h1><span class="tag">Private to you</span></div>
      <div class="kpis">
        ${kpi({ icon: 'users', tone: 'ip', n: users.length, label: `Users · ${active} active today` })}
        ${kpi({ icon: 'circle-check', tone: 'ok', n: sum('completed'), label: `Tasks completed of ${sum('tasks')}` })}
        ${kpi({ icon: 'target', tone: 'teal', n: `${avg}%`, label: 'Average completion' })}
        ${kpi({ icon: 'circle-alert', tone: 'bad', n: sum('overdue'), label: 'Overdue tasks' })}
      </div>
      <div class="grid-2">
        <section class="card">
          <div class="card-h"><h2 class="card-title">Completion by User</h2><span class="muted">Tasks done ÷ tasks assigned</span></div>
          <div class="card-body workload">${ranked.length ? ranked.map((u) => `<div class="wl-row" title="${esc(label(u))}: ${u.completed} of ${u.tasks} tasks done">
              <span class="wl-name">${face(u.id)}<span>${esc(label(u))}</span></span>
              <span class="wl-track"><span class="wl-fill" style="width:${u.completion}%">${u.completion ? '<i class="ok" style="flex:1"></i>' : ''}</span></span>
              <span class="wl-n">${u.completion}%</span></div>`).join('') : '<p class="note">Nobody has tasks yet.</p>'}</div>
        </section>
        <section class="card">
          <div class="card-h"><h2 class="card-title">Tasks by Status · All Users</h2></div>
          <div class="card-body donut-wrap">
            ${donut(segs, 172, 26, sum('tasks'), 'Tasks')}
            <ul class="dlegend">${segs.map((s) => `<li><i style="background:${s.color}"></i><span>${s.label}</span><b>${s.value}</b></li>`).join('')}</ul>
          </div>
        </section>
      </div>
      <section class="card">
        <div class="card-h"><h2 class="card-title">By Department</h2></div>
        <div class="table-scroll">
          <table class="dtable">
            <thead><tr><th>Department</th><th class="n">Users</th><th class="n">Tasks</th><th class="n">Done</th><th class="n">Overdue</th><th>Completion</th></tr></thead>
            <tbody>${deptRows.map((d) => `<tr><td>${esc(d.name)}</td><td class="n">${d.users}</td><td class="n">${d.tasks}</td><td class="n">${d.completed}</td><td class="n">${d.overdue}</td><td style="min-width:130px">${pctBar(d.tasks ? Math.round((100 * d.completed) / d.tasks) : 0)}</td></tr>`).join('')}</tbody>
          </table>
        </div>
      </section>
      <section class="card">
        <div class="card-h"><h2 class="card-title">All Users</h2><span class="muted">${plural(users.length, 'person', 'people')} · newest activity first</span></div>
        <div class="table-scroll">
          <table class="dtable">
            <thead><tr><th>User</th><th>Department</th><th class="n">Sign-ins</th><th>Last active</th><th class="n">Tasks</th><th class="n">Done</th><th class="n">Overdue</th><th>Completion</th><th class="n">Requests</th><th></th></tr></thead>
            <tbody>${users.map((u) => `<tr>
              <td><span class="who">${face(u.id, 'av-md')}<span>${esc(label(u))}${u.id === cloud.uid ? ' (you)' : ''}<small>${sub(u) || '&nbsp;'}</small></span></span></td>
              <td>${esc(u.dept)}</td>
              <td class="n">${u.signIns}</td>
              <td><span class="presence${u.signedIn ? ' on' : ''}"><i></i>${relTime(u.lastSeen) || '—'}</span></td>
              <td class="n">${u.tasks}</td><td class="n">${u.completed}</td><td class="n">${u.overdue}</td>
              <td style="min-width:130px">${pctBar(u.completion)}</td>
              <td class="n">${u.requests}</td>
              <td><button class="link" data-action="user-activity" data-id="${esc(u.id)}">Activity</button></td>
            </tr>`).join('')}</tbody>
          </table>
        </div>
      </section>
    </div>`;
  }

  /* ------------------------------------------------------------------ boot */

  function init() {
    try {
      const saved = localStorage.getItem(THEME_KEY);
      if (saved === 'light' || saved === 'dark' || saved === 'system') ui.theme = saved;
    } catch (e) { /* default to the system theme */ }
    if (ui.theme !== 'system') applyTheme();

    const hash = (location.hash || '').slice(1);
    if (VIEWS[hash]) ui.view = hash;

    document.addEventListener('click', onClick);
    document.addEventListener('change', onChange);
    document.addEventListener('submit', onSubmit);
    document.addEventListener('keydown', onKey);
    window.addEventListener('hashchange', () => {
      const h = (location.hash || '').slice(1);
      if (inApp() && VIEWS[h] && h !== ui.view) go(h);
    });
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    if (mq.addEventListener) mq.addEventListener('change', () => { if (inApp()) { renderChrome(); paintIcons(); } });
    window.matchMedia('(min-width: 1360px)').addEventListener('change', () => { if (inApp()) { ui.drawer = false; render(); } });

    // A fresh store (first visit, or the data format changed): give registered staff their onboarding again.
    if (state.seeded !== true) {
      accounts.forEach(seedOnboarding);
      state.seeded = true;
      save();
    }

    setInterval(tick, 4000);
    if (FB) { bootFirebase(); return; }
    const user = loadSession();
    if (user) resume(user); else mountAuth();
    initCloud();
  }

  // Opens the dashboard for someone who is already signed in.
  function resume(id) {
    ME = id;
    cloud.signedIn = true;
    cloud.account = accountSummary();
    cloud.lastPage = ui.view;
    const t = findTask(ui.selected);
    if (!t || !isMine(t)) ui.selected = firstOpenTaskId();
    mountApp();
    // One "returned" entry per browser session, not one per reload.
    let seen = false;
    try { seen = sessionStorage.getItem('asu-foe-smart-staff:visit') === '1'; sessionStorage.setItem('asu-foe-smart-staff:visit', '1'); } catch (e) { /* count it */ }
    if (!seen) track('visit', 'Returned to the dashboard');
  }

  // Firebase remembers who is signed in; wait for its answer before showing either screen.
  function bootFirebase() {
    $('#root').innerHTML = `<div class="boot">${icon('loader')}<span>Loading…</span></div>`;
    paintIcons();
    let settled = false;
    const fallback = setTimeout(() => { if (!settled) { settled = true; mountAuth(); } }, 8000);
    FB.auth.onAuthStateChanged(async (u) => {
      if (settled) return; // later changes come from this page's own sign-in and sign-out
      settled = true;
      clearTimeout(fallback);
      if (!u) { mountAuth(); return; }
      const acct = await accountFromCloud(u);
      resume(acct.id);
      initCloud();
    });
  }

  function bindShell() {
    const search = $('#search');
    search.addEventListener('input', () => {
      ui.search = search.value;
      if (ui.view === 'tasks' || ui.view === 'requests') render(); else renderSearchPop();
    });
    search.addEventListener('focus', renderSearchPop);
    search.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && ui.search.trim() && ui.view !== 'tasks' && ui.view !== 'requests') {
        ui.taskScope = 'all';
        ui.taskFilter = 'all';
        go('tasks', true);
      }
    });
  }

  function mountApp() {
    $('#root').innerHTML = shell();
    bindShell();
    try { history.replaceState(null, '', '#' + ui.view); } catch (e) { /* hash routing is optional */ }
    render();
    window.scrollTo(0, 0);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
