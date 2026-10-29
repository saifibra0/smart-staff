/*
 * Demo data for the ASU FOE Smart Staff Experience dashboard.
 *
 * Everything here is sample content. To connect the real system, replace
 * these arrays with responses from your API (ESP32 devices post sensor
 * readings to the server; the dashboard reads tasks, requests and campus
 * status from it). Field names below are the contract app.js expects.
 */
window.DEMO_DATA = {
  // The demo is pinned to one working day so every view tells the same story.
  today: '2026-10-20',
  nowMinutes: 9 * 60 + 30, // 09:30
  me: 'ahmed', // the demo account

  /*
   * Registration. For the demo, accounts are created and checked in the
   * browser; a real deployment must create them on the server instead.
   */
  auth: {
    allowedDomains: ['eng.asu.edu.eg'], // only faculty emails can register; [] accepts any email
    // Dashboard accounts with these emails are managers. On the published page the
    // manager must also be the page owner, because only the owner can read other staff's data.
    managerEmails: ['23p0317@eng.asu.edu.eg'],
    demoEmail: 'ahmed.hassan@eng.asu.edu.eg'
  },

  staff: [
    { id: 'ahmed',   name: 'Ahmed Hassan',     role: 'Administrative Staff',  dept: 'Dean\'s Office',   onSite: true },
    { id: 'sara',    name: 'Sara Mohamed',     role: 'Academic Coordinator',  dept: 'Academic Affairs', onSite: true },
    { id: 'mohamed', name: 'Mohamed Ali',      role: 'Facilities Officer',    dept: 'Facilities',       onSite: true },
    { id: 'nourhan', name: 'Nourhan Adel',     role: 'Lab Engineer',          dept: 'Laboratories',     onSite: true },
    { id: 'mostafa', name: 'Mostafa Ibrahim',  role: 'IT Support',            dept: 'IT Services',      onSite: true },
    { id: 'mariam',  name: 'Mariam Khaled',    role: 'Quality Assurance',     dept: 'Quality Unit',     onSite: false },
    { id: 'omar',    name: 'Omar Farouk',      role: 'Student Affairs',       dept: 'Student Affairs',  onSite: true },
    { id: 'yasmin',  name: 'Yasmin Samir',     role: 'Finance Officer',       dept: 'Finance',          onSite: true },
    { id: 'karim',   name: 'Karim Mahmoud',    role: 'Lab Technician',        dept: 'Laboratories',     onSite: false },
    { id: 'hana',    name: 'Hana Youssef',     role: 'HR Specialist',         dept: 'Human Resources',  onSite: true },
    { id: 'tarek',   name: 'Tarek Nabil',      role: 'Security Supervisor',   dept: 'Campus Security',  onSite: true },
    { id: 'laila',   name: 'Dr. Laila Mansour',role: 'Head of Department',    dept: 'Dean\'s Office',   onSite: true }
  ],

  /*
   * status: not-started | in-progress | awaiting | completed
   * priority: high | medium | low
   * owner = who does the work, by = who assigned it
   */
  tasks: [
    {
      id: 't1', title: 'Prepare department report', priority: 'high', owner: 'ahmed', by: 'laila',
      due: '2026-10-20', status: 'in-progress',
      desc: 'Prepare the monthly department report including academic activities, research, and administrative updates for submission to the faculty management.',
      subtasks: [
        { t: 'Collect data from department units', done: true },
        { t: 'Prepare initial draft', done: true },
        { t: 'Review with department head', done: true },
        { t: 'Include figures and statistics', done: false },
        { t: 'Finalize and submit', done: false }
      ],
      files: [
        { name: 'Department_Report_Template.docx', size: '245 KB', kind: 'doc' },
        { name: 'Data_Summary.xlsx', size: '128 KB', kind: 'sheet' }
      ],
      comments: [
        { by: 'sara', at: 'Today, 10:15 AM', text: 'Please include the latest research activities.' }
      ],
      log: [
        { by: 'ahmed', at: '10:12 AM', text: 'Updated task status to In Progress' },
        { by: 'sara', at: '10:15 AM', text: 'Added a comment' }
      ]
    },
    {
      id: 't2', title: 'Review exam timetable', priority: 'medium', owner: 'sara', by: 'ahmed',
      due: '2026-10-20', status: 'not-started',
      desc: 'Check the midterm timetable for room clashes and invigilator coverage before it is published to students.',
      subtasks: [
        { t: 'Check room clashes', done: false },
        { t: 'Confirm invigilators', done: false },
        { t: 'Send to student affairs', done: false }
      ],
      files: [{ name: 'Midterm_Timetable_v3.xlsx', size: '86 KB', kind: 'sheet' }],
      comments: [], log: [{ by: 'ahmed', at: 'Yesterday', text: 'Assigned task to Sara Mohamed' }]
    },
    {
      id: 't3', title: 'Approve maintenance request', priority: 'high', owner: 'mohamed', by: 'ahmed',
      due: '2026-10-20', status: 'awaiting',
      desc: 'AC maintenance for Room 203 (#SR-1045). The facilities team needs approval of the vendor quote before work starts.',
      subtasks: [
        { t: 'Review vendor quote', done: false },
        { t: 'Confirm budget line', done: false }
      ],
      files: [{ name: 'AC_Vendor_Quote.pdf', size: '312 KB', kind: 'doc' }],
      comments: [], log: [{ by: 'mohamed', at: '08:55 AM', text: 'Requested approval' }]
    },
    {
      id: 't4', title: 'Submit accreditation evidence', priority: 'medium', owner: 'ahmed', by: 'laila',
      due: '2026-10-22', status: 'in-progress',
      desc: 'Upload the evidence files requested by the accreditation committee for Standard 4 (teaching and learning).',
      subtasks: [
        { t: 'Gather course files', done: true },
        { t: 'Collect student survey results', done: true },
        { t: 'Scan signed committee minutes', done: false },
        { t: 'Upload to accreditation portal', done: false },
        { t: 'Notify quality unit', done: false }
      ],
      files: [], comments: [], log: [{ by: 'ahmed', at: 'Yesterday', text: 'Completed 2 subtasks' }]
    },
    {
      id: 't5', title: 'Update laboratory inventory', priority: 'low', owner: 'nourhan', by: 'ahmed',
      due: '2026-10-23', status: 'not-started',
      desc: 'Record new equipment received this month and flag items that need calibration.',
      subtasks: [
        { t: 'Count equipment in Lab 1', done: false },
        { t: 'Count equipment in Lab 2', done: false },
        { t: 'Update inventory sheet', done: false }
      ],
      files: [], comments: [], log: []
    },
    {
      id: 't6', title: 'Review meeting minutes', priority: 'low', owner: 'ahmed', by: 'ahmed',
      due: '2026-10-24', status: 'in-progress',
      desc: 'Review and circulate the minutes of last week\'s coordination meeting.',
      subtasks: [
        { t: 'Read draft minutes', done: true },
        { t: 'Correct action items', done: false },
        { t: 'Circulate to attendees', done: false }
      ],
      files: [], comments: [], log: []
    },
    {
      id: 't7', title: 'Approve lab equipment purchase', priority: 'high', owner: 'karim', by: 'ahmed',
      due: '2026-10-27', status: 'awaiting',
      desc: 'Purchase order for two digital oscilloscopes for the electronics lab.',
      subtasks: [], files: [{ name: 'PO_Oscilloscopes.pdf', size: '190 KB', kind: 'doc' }], comments: [],
      log: [{ by: 'karim', at: 'Yesterday', text: 'Requested approval' }]
    },
    // Completed this week (assigned by or owned by Ahmed)
    { id: 't8',  title: 'Book hall for accreditation visit', priority: 'medium', owner: 'ahmed', by: 'laila', due: '2026-10-19', status: 'completed', completedOn: '2026-10-19', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't9',  title: 'Send weekly attendance report', priority: 'low', owner: 'ahmed', by: 'laila', due: '2026-10-19', status: 'completed', completedOn: '2026-10-19', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't10', title: 'Update staff contact directory', priority: 'low', owner: 'hana', by: 'ahmed', due: '2026-10-18', status: 'completed', completedOn: '2026-10-18', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't11', title: 'Collect lab safety forms', priority: 'medium', owner: 'nourhan', by: 'ahmed', due: '2026-10-19', status: 'completed', completedOn: '2026-10-19', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't12', title: 'Schedule exam invigilators', priority: 'high', owner: 'sara', by: 'ahmed', due: '2026-10-19', status: 'completed', completedOn: '2026-10-19', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't13', title: 'Order office supplies', priority: 'low', owner: 'ahmed', by: 'ahmed', due: '2026-10-20', status: 'completed', completedOn: '2026-10-20', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't14', title: 'Archive September correspondence', priority: 'low', owner: 'ahmed', by: 'ahmed', due: '2026-10-21', status: 'completed', completedOn: '2026-10-20', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't15', title: 'Prepare agenda for coordination meeting', priority: 'medium', owner: 'ahmed', by: 'laila', due: '2026-10-20', status: 'completed', completedOn: '2026-10-20', desc: '', subtasks: [], files: [], comments: [], log: [] },

    // The rest of the department
    { id: 't16', title: 'Prepare semester budget summary', priority: 'high', owner: 'yasmin', by: 'laila', due: '2026-10-23', status: 'in-progress', desc: 'Summarize spending against the semester budget for the faculty council.', subtasks: [{ t: 'Export ledger', done: true }, { t: 'Reconcile lab spending', done: false }, { t: 'Write summary', done: false }], files: [], comments: [], log: [] },
    { id: 't17', title: 'Upgrade lab PCs to new image', priority: 'medium', owner: 'mostafa', by: 'laila', due: '2026-10-21', status: 'in-progress', desc: 'Re-image the 40 PCs in the computer lab with the new semester build.', subtasks: [{ t: 'Prepare image', done: true }, { t: 'Rows 1–4', done: true }, { t: 'Rows 5–8', done: false }], files: [], comments: [], log: [] },
    { id: 't18', title: 'Plan student orientation week', priority: 'medium', owner: 'omar', by: 'laila', due: '2026-10-26', status: 'in-progress', desc: '', subtasks: [{ t: 'Book auditorium', done: true }, { t: 'Confirm speakers', done: false }, { t: 'Print guides', done: false }, { t: 'Assign volunteers', done: false }], files: [], comments: [], log: [] },
    { id: 't19', title: 'Renew software licenses', priority: 'high', owner: 'mostafa', by: 'laila', due: '2026-10-19', status: 'in-progress', desc: 'MATLAB and AutoCAD campus licenses expire this month.', subtasks: [{ t: 'Get quotes', done: true }, { t: 'Submit purchase order', done: false }], files: [], comments: [], log: [] },
    { id: 't20', title: 'Draft quality assurance report', priority: 'high', owner: 'mariam', by: 'laila', due: '2026-10-22', status: 'in-progress', desc: '', subtasks: [{ t: 'Outline', done: true }, { t: 'Collect KPIs', done: true }, { t: 'Draft', done: false }, { t: 'Review', done: false }], files: [], comments: [], log: [] },
    { id: 't21', title: 'Coordinate visitor parking permits', priority: 'low', owner: 'tarek', by: 'laila', due: '2026-10-21', status: 'in-progress', desc: 'Issue temporary permits for the accreditation visitors.', subtasks: [{ t: 'Collect plate numbers', done: true }, { t: 'Register plates at gate', done: false }], files: [], comments: [], log: [] },
    { id: 't22', title: 'Update staff training records', priority: 'medium', owner: 'hana', by: 'laila', due: '2026-10-18', status: 'in-progress', desc: '', subtasks: [{ t: 'Import attendance sheets', done: true }, { t: 'Update HR system', done: false }], files: [], comments: [], log: [] },
    { id: 't23', title: 'Calibrate lab equipment', priority: 'medium', owner: 'karim', by: 'laila', due: '2026-10-24', status: 'in-progress', desc: '', subtasks: [{ t: 'Multimeters', done: true }, { t: 'Power supplies', done: false }, { t: 'Oscilloscopes', done: false }], files: [], comments: [], log: [] },
    { id: 't24', title: 'Compile course evaluation results', priority: 'medium', owner: 'sara', by: 'laila', due: '2026-10-23', status: 'in-progress', desc: '', subtasks: [{ t: 'Download survey results', done: true }, { t: 'Build summary tables', done: false }], files: [], comments: [], log: [] },
    { id: 't25', title: 'Review fire safety checklist', priority: 'high', owner: 'tarek', by: 'laila', due: '2026-10-21', status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't26', title: 'Organize research seminar', priority: 'low', owner: 'sara', by: 'laila', due: '2026-10-29', status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't27', title: 'Inventory chemistry lab reagents', priority: 'low', owner: 'karim', by: 'laila', due: '2026-10-28', status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't28', title: 'Prepare payroll adjustments', priority: 'medium', owner: 'yasmin', by: 'laila', due: '2026-10-17', status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't29', title: 'Set up RFID cards for new staff', priority: 'medium', owner: 'mostafa', by: 'laila', due: '2026-10-22', status: 'not-started', desc: 'Register three new staff ID cards on the RC522 gate readers.', subtasks: [], files: [], comments: [], log: [] },
    { id: 't30', title: 'Plan recruitment interviews', priority: 'low', owner: 'hana', by: 'laila', due: '2026-10-30', status: 'not-started', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't31', title: 'Approve overtime requests', priority: 'medium', owner: 'hana', by: 'laila', due: '2026-10-21', status: 'awaiting', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't32', title: 'Approve conference travel', priority: 'low', owner: 'mariam', by: 'laila', due: '2026-10-23', status: 'awaiting', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't33', title: 'Approve AC replacement quote', priority: 'high', owner: 'mohamed', by: 'laila', due: '2026-10-16', status: 'awaiting', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't34', title: 'Approve exam hall booking', priority: 'medium', owner: 'omar', by: 'laila', due: '2026-10-22', status: 'awaiting', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't35', title: 'Fix network outage in Building B', priority: 'high', owner: 'mostafa', by: 'laila', due: '2026-10-19', status: 'completed', completedOn: '2026-10-19', desc: '', subtasks: [], files: [], comments: [], log: [] },
    { id: 't36', title: 'Distribute lecture schedules', priority: 'medium', owner: 'sara', by: 'laila', due: '2026-10-18', status: 'completed', completedOn: '2026-10-18', desc: '', subtasks: [], files: [], comments: [], log: [] }
  ],

  // kind: meeting | inspection | discussion | review   (drives the colour)
  events: [
    { id: 'e1',  date: '2026-10-19', start: '10:00', end: '11:00', title: 'Weekly Planning', place: 'Room 203', kind: 'meeting' },
    { id: 'e2',  date: '2026-10-19', start: '13:00', end: '14:00', title: 'Exam Committee', place: 'Room 110', kind: 'review' },
    { id: 'e3',  date: '2026-10-20', start: '11:00', end: '12:00', title: 'Department Coordination Meeting', place: 'Main Building – Room 203', kind: 'meeting' },
    { id: 'e4',  date: '2026-10-20', start: '14:00', end: '15:00', title: 'Accreditation Committee Follow-up', place: 'Online Meeting', kind: 'discussion', online: true },
    { id: 'e5',  date: '2026-10-20', start: '16:00', end: '17:00', title: 'Laboratory Development Discussion', place: 'Room 105', kind: 'review' },
    { id: 'e6',  date: '2026-10-21', start: '09:00', end: '10:00', title: 'IT Systems Review', place: 'IT Office', kind: 'review' },
    { id: 'e7',  date: '2026-10-21', start: '12:00', end: '13:00', title: 'Safety Walkthrough', place: 'Labs Building', kind: 'inspection' },
    { id: 'e8',  date: '2026-10-22', start: '09:00', end: '10:00', title: 'Department Meeting', place: 'Room 203', kind: 'meeting' },
    { id: 'e9',  date: '2026-10-22', start: '11:00', end: '12:00', title: 'Lab Inspection', place: 'Lab 2', kind: 'inspection' },
    { id: 'e10', date: '2026-10-22', start: '13:00', end: '14:00', title: 'Team Discussion', place: 'Room 105', kind: 'discussion' },
    { id: 'e11', date: '2026-10-22', start: '15:00', end: '16:00', title: 'Project Review', place: 'Room 203', kind: 'review' },
    { id: 'e12', date: '2026-10-25', start: '10:00', end: '11:30', title: 'Quality Audit Preparation', place: 'Quality Unit', kind: 'meeting' }
  ],

  // status: in-progress | awaiting | approved | completed | rejected
  requests: [
    { id: 'SR-1045', title: 'AC Maintenance – Room 203', cat: 'maintenance', location: 'Main Building', by: 'ahmed', date: '2026-10-18', status: 'in-progress' },
    { id: 'SR-1044', title: 'Lighting Repair – Corridor', cat: 'electrical', location: 'Building B, 2nd floor', by: 'omar', date: '2026-10-17', status: 'awaiting' },
    { id: 'SR-1043', title: 'Projector for Meeting Room', cat: 'equipment', location: 'Room 105', by: 'ahmed', date: '2026-10-16', status: 'completed' },
    { id: 'SR-1042', title: 'New Office Chair Request', cat: 'furniture', location: 'Office 112', by: 'hana', date: '2026-10-15', status: 'approved' },
    { id: 'SR-1041', title: 'Water Leak – Lab 2 Restroom', cat: 'maintenance', location: 'Labs Building', by: 'nourhan', date: '2026-10-14', status: 'completed' },
    { id: 'SR-1040', title: 'Network Port – Office 112', cat: 'it', location: 'Office 112', by: 'yasmin', date: '2026-10-13', status: 'in-progress' }
  ],

  emails: [
    { subject: 'Faculty Meeting Invitation', from: 'eng-dept@university.edu', at: '10:30 AM', unread: true },
    { subject: 'Maintenance Request Update', from: 'facilities@university.edu', at: '09:15 AM', unread: true, flag: true },
    { subject: 'Accreditation Documents', from: 'quality@university.edu', at: 'Yesterday', unread: false }
  ],

  notifications: [
    { id: 'n1', icon: 'message-square', text: 'Sara Mohamed commented on “Prepare department report”', at: '10:15 AM', unread: true, task: 't1' },
    { id: 'n2', icon: 'hourglass', text: 'SR-1044 Lighting Repair is waiting for your approval', at: '09:20 AM', unread: true, view: 'approvals' },
    { id: 'n3', icon: 'square-parking', text: 'Parking space P12 assigned on arrival', at: '08:45 AM', unread: false, view: 'campus' }
  ],

  // ---- Smart campus (ESP32 + sensors) ----
  campus: {
    parkingZones: [
      { zone: 'A', label: 'Zone A – Staff', slots: 24 },
      { zone: 'B', label: 'Zone B – Staff & Visitors', slots: 26 }
    ],
    // slot numbers that start occupied (the rest are free)
    occupied: [1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 21, 22, 23, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 40, 41, 43, 44, 46],
    mySlot: 12,
    gate: { name: 'Main Gate', open: false, last: '08:42' },
    plate: 'CA 1234',
    environment: { temp: 24.6, humidity: 41, room: 'Room 203' },
    occupancy: [
      { area: 'Main Building', people: 142, capacity: 220 },
      { area: 'Labs Building', people: 38, capacity: 90 },
      { area: 'Library', people: 57, capacity: 120 },
      { area: 'Admin Offices', people: 21, capacity: 40 }
    ],
    accessLog: [
      { time: '08:42', who: 'ahmed', method: 'Plate CA 1234', point: 'Main Gate', result: 'granted' },
      { time: '08:39', who: 'sara', method: 'RFID card', point: 'Pedestrian Gate', result: 'granted' },
      { time: '08:35', who: null, method: 'Plate SN 4821', point: 'Main Gate', result: 'denied' },
      { time: '08:31', who: 'mohamed', method: 'Plate GZ 7710', point: 'Main Gate', result: 'granted' },
      { time: '08:24', who: 'tarek', method: 'RFID card', point: 'Building Entrance', result: 'granted' },
      { time: '08:12', who: 'mostafa', method: 'Plate CA 5502', point: 'Main Gate', result: 'granted' }
    ],
    devices: [
      { name: 'ESP32', role: 'Main Controller', detail: 'Wi-Fi + IoT · −58 dBm', icon: 'cpu', online: 1, total: 1 },
      { name: 'RFID RC522', role: 'Staff ID Access', detail: 'Gate & building readers', icon: 'credit-card', online: 3, total: 3 },
      { name: 'HC-SR04', role: 'Ultrasonic Parking Detection', detail: 'One per parking slot', icon: 'radar', online: 49, total: 50 },
      { name: 'ESP32-CAM', role: 'License Plate / Entrance Camera', detail: 'Main gate lane', icon: 'camera', online: 1, total: 1 },
      { name: 'PIR Motion Sensor', role: 'Occupancy Detection', detail: 'Offices & entries', icon: 'activity', online: 6, total: 6 },
      { name: 'DHT22', role: 'Temperature & Humidity', detail: 'Optional · Room 203', icon: 'thermometer', online: 1, total: 1 }
    ],
    journey: [
      { title: 'Arrive at Campus', text: 'Approach the faculty entrance by car or on foot.', time: '08:40', icon: 'car' },
      { title: 'Verify Staff ID', text: 'Automatic ID verification and gate access.', time: '08:42', icon: 'scan-face' },
      { title: 'Locate Parking', text: 'Assigned parking space P12 shown on the phone.', time: '08:45', icon: 'square-parking' },
      { title: 'Open Today\'s Tasks', text: 'View tasks, meetings and notifications.', time: '09:05', icon: 'list-checks' },
      { title: 'Update & Delegate', text: 'Update progress and delegate to the team.', time: 'Now', icon: 'users' },
      { title: 'End of Day Report', text: 'Completed tasks and tomorrow\'s schedule.', time: '17:00', icon: 'clipboard-check' }
    ],
    journeyStep: 4 // index of the step in progress
  }
};
