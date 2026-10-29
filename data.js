/*
 * Starting data for the ASU FOE Smart Staff Experience dashboard.
 *
 * There are no sample people: staff appear when they create accounts, and
 * each new account starts with an onboarding checklist. The calendar, inbox
 * and campus sensor values below are examples. To connect the real system,
 * replace them with responses from your API (ESP32 devices post sensor
 * readings to the server). Field names below are the contract app.js expects.
 */
window.DEMO_DATA = {
  // The dashboard is pinned to one working day so the example calendar lines up.
  today: '2026-10-20',
  nowMinutes: 9 * 60 + 30, // 09:30

  /*
   * Registration. Accounts are created and checked in the browser; a real
   * deployment must create them on the server instead.
   */
  auth: {
    allowedDomains: ['eng.asu.edu.eg'], // only faculty emails can register; [] accepts any email
    // Dashboard accounts with these emails are managers. On the published Claude page the
    // manager must also be the page owner, because only the owner can read other staff's data.
    managerEmails: ['23p0317@eng.asu.edu.eg']
  },

  /*
   * Firebase project "smart-staff" (free Spark plan): sign-in and the shared
   * database the manager reads. These values only identify the project and are
   * meant to be public; access is controlled by the Firestore security rules.
   */
  firebase: {
    apiKey: 'AIzaSyCRhKAvNMRPp4kJle02YklmN8P8uXeQK0Y',
    authDomain: 'smart-staff-2de6a.firebaseapp.com',
    projectId: 'smart-staff-2de6a',
    storageBucket: 'smart-staff-2de6a.firebasestorage.app',
    messagingSenderId: '360538043305',
    appId: '1:360538043305:web:4956327dec754d1cdb4ab8'
  },

  // Offered when staff sign up.
  departments: [
    'Dean\'s Office', 'Academic Affairs', 'Facilities', 'Laboratories', 'IT Services',
    'Quality Unit', 'Student Affairs', 'Finance', 'Human Resources', 'Campus Security'
  ],

  // Staff join these lists through sign-up and their own work; nothing is pre-filled.
  staff: [],
  tasks: [],
  requests: [],
  notifications: [],

  // Department calendar. kind: meeting | inspection | discussion | review   (drives the colour)
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

  emails: [
    { subject: 'Faculty Meeting Invitation', from: 'eng-dept@university.edu', at: '10:30 AM', unread: true },
    { subject: 'Maintenance Request Update', from: 'facilities@university.edu', at: '09:15 AM', unread: true, flag: true },
    { subject: 'Accreditation Documents', from: 'quality@university.edu', at: 'Yesterday', unread: false }
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
    environment: { temp: 24.6, humidity: 41, room: 'Room 203' },
    occupancy: [
      { area: 'Main Building', people: 142, capacity: 220 },
      { area: 'Labs Building', people: 38, capacity: 90 },
      { area: 'Library', people: 57, capacity: 120 },
      { area: 'Admin Offices', people: 21, capacity: 40 }
    ],
    // who: a staff id, or null with a label for anyone not shown by name
    accessLog: [
      { time: '08:42', who: null, label: 'Staff vehicle', method: 'Plate CA 1234', point: 'Main Gate', result: 'granted' },
      { time: '08:39', who: null, label: 'Staff card', method: 'RFID card', point: 'Pedestrian Gate', result: 'granted' },
      { time: '08:35', who: null, label: 'Unknown vehicle', method: 'Plate SN 4821', point: 'Main Gate', result: 'denied' },
      { time: '08:31', who: null, label: 'Staff vehicle', method: 'Plate GZ 7710', point: 'Main Gate', result: 'granted' },
      { time: '08:24', who: null, label: 'Staff card', method: 'RFID card', point: 'Building Entrance', result: 'granted' },
      { time: '08:12', who: null, label: 'Staff vehicle', method: 'Plate CA 5502', point: 'Main Gate', result: 'granted' }
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
