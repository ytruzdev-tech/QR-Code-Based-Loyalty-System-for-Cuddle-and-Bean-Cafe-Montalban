#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include "LittleFS.h"
#include <time.h>
#include <esp_task_wdt.h>

// ─── CONFIG ─────────────────────────────────────────────────
#define MAX_CUSTOMERS   10
#define MAX_HISTORY     10
#define WDT_TIMEOUT     15
#define TOUCH_THRESHOLD 30
#define POINTS_PER_SCAN 1       // 1 point per scan
#define REDEEM_EVERY    10      // every 10 points = 1 reward

// ─── WIFI ────────────────────────────────────────────────────
const char* ap_ssid     = "CafeLoyalty_AP";
const char* ap_pass     = "@CafeLoyalty2025";
const char* router_ssid = "Pau-2.4G";
const char* router_pass = "@xiankalbosharxl123@@@";

// ─── STAFF LOGIN ─────────────────────────────────────────────
const char* STAFF_EMAIL = "jhairuz";
const char* STAFF_PASS  = "marcos123";

// ─── HARDWARE ────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);
HardwareSerial QRSerial(2);  // RX=16, TX=17

// ─── DATA STRUCTURES ─────────────────────────────────────────
struct ScanHistory {
  String datetime;
  int    pointsEarned;
  String note; // "EARNED" or "REDEEMED"
};

struct Customer {
  String      id;           // unique QR id (e.g. CAFE_001)
  String      name;
  int         totalScans;
  int         totalPoints;
  int         totalRedeemed;
  ScanHistory history[MAX_HISTORY];
  int         historyCount;
};

Customer customers[MAX_CUSTOMERS];
int customerCount = 0;

// ─── STATE ────────────────────────────────────────────────────
bool systemEnabled   = true;
bool staffLoggedIn   = false;
bool adminQR_Detected = false;
unsigned long lastScanMillis = 0;

// ─── HELPERS ─────────────────────────────────────────────────
String getDateTime() {
  struct tm t;
  if (!getLocalTime(&t)) return "0000-00-00 00:00";
  char buf[20];
  strftime(buf, 20, "%Y-%m-%d %I:%M %p", &t);
  return String(buf);
}

String getDate() {
  struct tm t;
  if (!getLocalTime(&t)) return "0000-00-00";
  char buf[11];
  strftime(buf, 11, "%Y-%m-%d", &t);
  return String(buf);
}

String getTime() {
  struct tm t;
  if (!getLocalTime(&t)) return "00:00 AM";
  char buf[12];
  strftime(buf, 12, "%I:%M %p", &t);
  return String(buf);
}

// ─── FILE STORAGE ────────────────────────────────────────────
void saveToFile() {
  if (LittleFS.usedBytes() > LittleFS.totalBytes() * 0.9) return;
  File f = LittleFS.open("/loyalty.txt", "w");
  if (!f) return;
  f.println("COUNT:" + String(customerCount));
  for (int i = 0; i < customerCount; i++) {
    f.println("ID:" + customers[i].id);
    f.println("NM:" + customers[i].name);
    f.println("SC:" + String(customers[i].totalScans));
    f.println("PT:" + String(customers[i].totalPoints));
    f.println("RD:" + String(customers[i].totalRedeemed));
    f.println("HC:" + String(customers[i].historyCount));
    for (int j = 0; j < customers[i].historyCount; j++) {
      f.println("H:" + customers[i].history[j].datetime + "|" +
                String(customers[i].history[j].pointsEarned) + "|" +
                customers[i].history[j].note);
    }
  }
  f.close();
}

void loadFromFile() {
  if (!LittleFS.exists("/loyalty.txt")) return;
  File f = LittleFS.open("/loyalty.txt", "r");
  if (!f) return;
  int ci = -1;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("COUNT:")) {
      customerCount = line.substring(6).toInt();
      if (customerCount > MAX_CUSTOMERS) customerCount = 0;
    } else if (line.startsWith("ID:")) {
      ci++;
      if (ci < MAX_CUSTOMERS) customers[ci].id = line.substring(3);
    } else if (ci >= 0 && ci < MAX_CUSTOMERS) {
      if      (line.startsWith("NM:")) customers[ci].name           = line.substring(3);
      else if (line.startsWith("SC:")) customers[ci].totalScans     = line.substring(3).toInt();
      else if (line.startsWith("PT:")) customers[ci].totalPoints    = line.substring(3).toInt();
      else if (line.startsWith("RD:")) customers[ci].totalRedeemed  = line.substring(3).toInt();
      else if (line.startsWith("HC:")) customers[ci].historyCount   = line.substring(3).toInt();
      else if (line.startsWith("H:")) {
        String d = line.substring(2);
        int p1 = d.lastIndexOf('|');
        int p0 = d.lastIndexOf('|', p1 - 1);
        int filledH = 0;
        for (int j = 0; j < MAX_HISTORY; j++) {
          if (customers[ci].history[j].datetime.length() > 0) filledH++;
          else break;
        }
        if (filledH < MAX_HISTORY && p0 != -1 && p1 != -1) {
          customers[ci].history[filledH].datetime = d.substring(0, p0);
          customers[ci].history[filledH].pointsEarned = d.substring(p0 + 1, p1).toInt();
          customers[ci].history[filledH].note = d.substring(p1 + 1);
        }
      }
    }
  }
  f.close();
}

// ─── FIND / CREATE CUSTOMER ───────────────────────────────────
int findCustomer(String id) {
  for (int i = 0; i < customerCount; i++)
    if (customers[i].id.equalsIgnoreCase(id)) return i;
  return -1;
}

void addHistory(int idx, int pts, String note) {
  Customer &c = customers[idx];
  int hc = c.historyCount;
  if (hc < MAX_HISTORY) {
    c.history[hc] = { getDateTime(), pts, note };
    c.historyCount++;
  }
}

// ─── CSS / HEAD ───────────────────────────────────────────────
String getHead(String title, bool dashboard = false) {
  String refreshTag = dashboard ? "<meta http-equiv='refresh' content='5'>" : "";
  return R"raw(<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
)raw" + refreshTag + R"raw(
<title>)raw" + title + R"raw(</title>
<link href="https://fonts.googleapis.com/css2?family=Playfair+Display:wght@600;700&family=DM+Sans:wght@300;400;500&display=swap" rel="stylesheet">
<style>
  :root{
    --bg:#0d0a06;
    --surface:#1a1510;
    --card:#231e17;
    --border:#3a3028;
    --gold:#d4a843;
    --gold2:#f0c060;
    --cream:#f5e6c8;
    --green:#4caf7d;
    --red:#e05555;
    --text:#e8dcc8;
    --muted:#8a7a65;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);font-family:'DM Sans',sans-serif;color:var(--text);min-height:100vh;}
  body::before{content:'';position:fixed;inset:0;background:radial-gradient(ellipse at 20% 0%,rgba(212,168,67,.07) 0%,transparent 60%),radial-gradient(ellipse at 80% 100%,rgba(76,175,125,.05) 0%,transparent 60%);pointer-events:none;}

  /* NAV */
  nav{background:var(--surface);border-bottom:1px solid var(--border);padding:0 32px;display:flex;align-items:center;justify-content:space-between;height:60px;position:sticky;top:0;z-index:100;backdrop-filter:blur(10px);}
  .nav-brand{font-family:'Playfair Display',serif;font-size:20px;color:var(--gold);letter-spacing:1px;}
  .nav-brand span{color:var(--cream);font-size:13px;display:block;font-family:'DM Sans',sans-serif;font-weight:300;letter-spacing:2px;text-transform:uppercase;}
  .nav-links a{color:var(--muted);text-decoration:none;font-size:13px;font-weight:500;padding:6px 14px;border-radius:6px;transition:.2s;}
  .nav-links a:hover{background:var(--card);color:var(--cream);}
  .nav-links .btn-danger{color:var(--red);border:1px solid rgba(224,85,85,.3);}
  .nav-links .btn-danger:hover{background:rgba(224,85,85,.1);}

  /* LAYOUT */
  .page{max-width:1100px;margin:0 auto;padding:32px 24px;}
  h1{font-family:'Playfair Display',serif;font-size:28px;color:var(--gold);margin-bottom:4px;}
  .subtitle{color:var(--muted);font-size:13px;margin-bottom:32px;font-weight:300;}

  /* STATS ROW */
  .stats-row{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:16px;margin-bottom:32px;}
  .stat-card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:20px;text-align:center;position:relative;overflow:hidden;}
  .stat-card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;background:linear-gradient(90deg,transparent,var(--gold),transparent);}
  .stat-num{font-family:'Playfair Display',serif;font-size:36px;color:var(--gold2);line-height:1;}
  .stat-lbl{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:1px;margin-top:6px;}

  /* TABLE */
  .table-wrap{background:var(--card);border:1px solid var(--border);border-radius:16px;overflow:hidden;}
  .table-header{padding:18px 24px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--border);}
  .table-header h3{font-family:'Playfair Display',serif;font-size:16px;color:var(--cream);}
  table{width:100%;border-collapse:collapse;}
  thead th{background:var(--surface);padding:12px 16px;text-align:left;font-size:11px;text-transform:uppercase;letter-spacing:1px;color:var(--muted);font-weight:500;border-bottom:1px solid var(--border);}
  tbody td{padding:14px 16px;border-bottom:1px solid rgba(58,48,40,.5);font-size:14px;vertical-align:middle;}
  tbody tr:last-child td{border-bottom:none;}
  tbody tr:hover td{background:rgba(212,168,67,.03);}

  /* BADGES */
  .badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:500;}
  .badge-gold{background:rgba(212,168,67,.15);color:var(--gold2);border:1px solid rgba(212,168,67,.25);}
  .badge-green{background:rgba(76,175,125,.12);color:var(--green);border:1px solid rgba(76,175,125,.2);}
  .badge-muted{background:rgba(138,122,101,.1);color:var(--muted);}

  /* POINTS BAR */
  .points-bar-wrap{width:100%;background:var(--surface);border-radius:99px;height:6px;overflow:hidden;margin-top:4px;}
  .points-bar{height:100%;background:linear-gradient(90deg,var(--gold),var(--gold2));border-radius:99px;transition:width .4s;}

  /* BUTTONS */
  .btn{display:inline-block;padding:9px 18px;border-radius:8px;font-size:13px;font-weight:500;cursor:pointer;text-decoration:none;border:none;transition:.2s;font-family:'DM Sans',sans-serif;}
  .btn-primary{background:var(--gold);color:#1a1510;}
  .btn-primary:hover{background:var(--gold2);}
  .btn-ghost{background:transparent;color:var(--muted);border:1px solid var(--border);}
  .btn-ghost:hover{border-color:var(--gold);color:var(--gold);}
  .btn-red{background:transparent;color:var(--red);border:1px solid rgba(224,85,85,.3);}
  .btn-red:hover{background:rgba(224,85,85,.1);}
  .btn-green{background:var(--green);color:#fff;}
  .btn-sm{padding:6px 12px;font-size:12px;}

  /* SEARCH */
  .search-bar{padding:10px 16px;background:var(--surface);border:1px solid var(--border);border-radius:9px;color:var(--text);font-size:14px;width:220px;font-family:'DM Sans',sans-serif;outline:none;transition:.2s;}
  .search-bar:focus{border-color:var(--gold);}

  /* LOGIN PAGE */
  .login-wrap{min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg);}
  .login-card{background:var(--card);border:1px solid var(--border);border-radius:22px;padding:44px 38px;width:100%;max-width:400px;text-align:center;position:relative;overflow:hidden;box-shadow:0 40px 80px rgba(0,0,0,.6);}
  .login-card::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:linear-gradient(90deg,var(--gold),var(--gold2),var(--gold));}
  .login-logo{font-family:'Playfair Display',serif;font-size:26px;color:var(--gold);margin-bottom:6px;}
  .login-logo span{display:block;font-family:'DM Sans',sans-serif;font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:3px;font-weight:300;}
  .form-group{margin-bottom:14px;text-align:left;position:relative;}
  .form-group label{display:block;font-size:11px;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:6px;}
  .form-input{width:100%;padding:13px 16px;background:var(--surface);border:1px solid var(--border);border-radius:9px;color:var(--text);font-size:14px;font-family:'DM Sans',sans-serif;outline:none;transition:.2s;}
  .form-input:focus{border-color:var(--gold);}
  .btn-login{width:100%;padding:14px;background:var(--gold);color:#1a1510;font-weight:600;font-size:15px;border:none;border-radius:10px;cursor:pointer;font-family:'Playfair Display',serif;letter-spacing:1px;margin-top:8px;transition:.2s;}
  .btn-login:hover{background:var(--gold2);}

  /* EYE ICON */
  .eye-btn { position: absolute; right: 12px; top: 34px; background: none; border: none; color: var(--muted); cursor: pointer; display: flex; align-items: center; }
  .eye-btn:hover { color: var(--gold); }

  /* MODAL */
  .modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.75);z-index:200;align-items:center;justify-content:center;backdrop-filter:blur(4px);}
  .modal-overlay.active{display:flex;}
  .modal-box{background:var(--card);border:1px solid var(--border);border-radius:18px;padding:32px;width:90%;max-width:560px;max-height:80vh;overflow-y:auto;position:relative;}
  .modal-title{font-family:'Playfair Display',serif;font-size:20px;color:var(--gold);margin-bottom:20px;}
  .modal-close{position:absolute;top:16px;right:20px;background:none;border:none;color:var(--muted);font-size:20px;cursor:pointer;}
  .modal-close:hover{color:var(--cream);}

  /* HISTORY TABLE in modal */
  .hist-table{width:100%;}
  .hist-table th{font-size:11px;color:var(--muted);}
  .hist-table td{font-size:13px;}

  /* SYSTEM TOGGLE */
  .sys-badge{display:inline-block;padding:5px 14px;border-radius:99px;font-size:12px;font-weight:600;letter-spacing:.5px;}
  .sys-on{background:rgba(76,175,125,.15);color:var(--green);border:1px solid rgba(76,175,125,.3);}
  .sys-off{background:rgba(224,85,85,.12);color:var(--red);border:1px solid rgba(224,85,85,.25);}

  /* REDEEM HIGHLIGHT */
  .redeem-ready{color:var(--gold2);font-weight:600;}

  /* EMPTY STATE */
  .empty{text-align:center;padding:48px;color:var(--muted);}
  .empty-icon{font-size:40px;margin-bottom:12px;}

  @media(max-width:600px){
    .page{padding:16px;}
    .stats-row{grid-template-columns:1fr 1fr;}
    .table-header{flex-direction:column;gap:12px;align-items:flex-start;}
    .search-bar{width:100%;}
  }
</style>
<script>
  function togglePass() {
    let p = prompt("Enter PIN to see password");
    if(p === "0827") {
      let x = document.getElementById("passField");
      x.type = (x.type === "password") ? "text" : "password";
    } else if(p !== null) { alert("Wrong PIN"); }
  }
  function checkLogin() {
    let p = prompt("Enter PIN to Sign In");
    return (p === "0827");
  }
  function confirmRemove(url, name) {
    let p = prompt("Enter PIN to remove " + name);
    if(p === "0827") { window.location.href = url; }
    else if(p !== null) { alert("Wrong PIN. Action cancelled."); }
  }
</script>
</head>)raw";
}

// ─── HANDLE ROOT / DASHBOARD ──────────────────────────────────
void handleRoot() {
  if (!staffLoggedIn) {
    String page = getHead("Cafe Loyalty — Login");
    page += R"raw(
<body>
<div class="login-wrap">
  <div class="login-card">
    <div class="login-logo">Cuddle & Bean Café <span>Staff Portal</span></div>
    <br>
    <form action="/login" method="POST" onsubmit="return checkLogin()">
      <div class="form-group">
        <label>Username</label>
        <input class="form-input" type="text" name="user" placeholder="staff username" required autocomplete="off">
      </div>
      <div class="form-group">
        <label>Password</label>
        <input class="form-input" type="password" name="pass" id="passField" placeholder="••••••••" required>
        <button type="button" class="eye-btn" onclick="togglePass()">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>
        </button>
      </div>
      <button type="submit" class="btn-login">Sign In</button>
    </form>
  </div>
</div>
</body></html>)raw";
    server.send(200, "text/html", page);
    return;
  }

  int total = customerCount;
  int totalScans = 0, totalRedeems = 0, redeemReady = 0;
  for (int i = 0; i < customerCount; i++) {
    totalScans += customers[i].totalScans;
    totalRedeems += customers[i].totalRedeemed;
    if (customers[i].totalPoints >= REDEEM_EVERY) redeemReady++;
  }

  String page = getHead("Café Loyalty — Dashboard", true);
  page += "<body>";
  page += R"raw(<nav>
  <div class="nav-brand">Cuddle & Bean Café<span>Loyalty Management</span></div>
  <div class="nav-links">
    <a href="/download"> Export CSV</a>
    <a href="/logout" class="btn-danger">Sign Out</a>
  </div>
</nav>)raw";

  page += "<div class='page'>";
  page += "<h1>Dashboard</h1>";
  page += "<p class='subtitle'>System Status: ";
  page += systemEnabled ? "<span class='sys-badge sys-on'>● ONLINE</span>" : "<span class='sys-badge sys-off'>● OFFLINE</span>";
  page += "</p>";

  page += "<div class='stats-row'>";
  page += "<div class='stat-card'><div class='stat-num'>" + String(total) + "</div><div class='stat-lbl'>Customers</div></div>";
  page += "<div class='stat-card'><div class='stat-num'>" + String(totalScans) + "</div><div class='stat-lbl'>Total Scans</div></div>";
  page += "<div class='stat-card'><div class='stat-num'>" + String(totalRedeems) + "</div><div class='stat-lbl'>Redeemed</div></div>";
  page += "<div class='stat-card'><div class='stat-num'>" + String(redeemReady) + "</div><div class='stat-lbl'>Ready to Redeem</div></div>";
  page += "</div>";

  page += "<div class='table-wrap'>";
  page += "<div class='table-header'><h3>Customer Records</h3>";
  page += "<input class='search-bar' id='srch' onkeyup='doSearch()' placeholder='Search name or ID…'></div>";
  page += "<table id='ctable'><thead><tr>";
  page += "<th>Customer ID</th><th>Name</th><th>Total Scans</th><th>Points</th><th>Redeemed</th><th>Actions</th>";
  page += "</tr></thead><tbody>";

  if (customerCount == 0) {
    page += "<tr><td colspan='6' class='empty'><div class='empty-icon'></div>No customers yet. Start scanning!</td></tr>";
  }

  for (int i = 0; i < customerCount; i++) {
    Customer &c = customers[i];
    int pct = min(100, (c.totalPoints % REDEEM_EVERY) * 100 / REDEEM_EVERY);
    bool rdy = c.totalPoints >= REDEEM_EVERY;

    page += "<tr>";
    page += "<td><span class='badge badge-muted'>" + c.id + "</span></td>";
    page += "<td>" + c.name + "</td>";
    page += "<td>" + String(c.totalScans) + "</td>";
    page += "<td>";
    page += rdy ? "<span class='redeem-ready'>★ " + String(c.totalPoints) + " pts</span>" : "<b>" + String(c.totalPoints) + "</b> / " + String(REDEEM_EVERY);
    page += "<div class='points-bar-wrap'><div class='points-bar' style='width:" + String(pct) + "%'></div></div></td>";
    page += "<td>" + String(c.totalRedeemed) + "x</td>";
    page += "<td style='display:flex;gap:8px;'>";
    page += "<button class='btn btn-ghost btn-sm' onclick='showHist(" + String(i) + ")'>View</button>";
    if (rdy) page += "<a href='/redeem?idx=" + String(i) + "' class='btn btn-primary btn-sm' onclick='return confirm(\"Redeem reward for " + c.name + "?\")'>Redeem</a>";
    page += "<button class='btn btn-red btn-sm' onclick='confirmRemove(\"/remove?idx=" + String(i) + "\", \"" + c.name + "\")'>Remove</button>";
    page += "</td></tr>";
  }

  page += "</tbody></table></div></div>";
  page += "<div class='modal-overlay' id='histModal'><div class='modal-box'>";
  page += "<button class='modal-close' onclick='closeModal()'>✕</button>";
  page += "<div class='modal-title' id='modalTitle'>History</div>";
  page += "<table class='hist-table'><thead><tr><th>Date & Time</th><th>Points</th><th>Note</th></tr></thead><tbody id='histBody'></tbody></table>";
  page += "</div></div>";

  page += "<script>";
  page += "var histData=[";
  for (int i = 0; i < customerCount; i++) {
    page += "{name:\"" + customers[i].name + "\",rows:[";
    for (int j = 0; j < customers[i].historyCount; j++) {
      page += "{dt:\"" + customers[i].history[j].datetime + "\",pt:" + String(customers[i].history[j].pointsEarned) + ",note:\"" + customers[i].history[j].note + "\"}";
      if (j < customers[i].historyCount - 1) page += ",";
    }
    page += "]}";
    if (i < customerCount - 1) page += ",";
  }
  page += "];";
  page += R"js(
function showHist(idx){
  var d=histData[idx];
  document.getElementById('modalTitle').textContent=d.name+' — History';
  var body='';
  if(d.rows.length===0){body='<tr><td colspan="3" style="text-align:center;color:#8a7a65;padding:24px;">No history yet</td></tr>';}
  else{
    d.rows.slice().reverse().forEach(function(r){
      var badgeClass=r.note==='REDEEMED'?'badge-green':'badge-gold';
      body+='<tr><td>'+r.dt+'</td><td>'+r.pt+'</td><td><span class="badge '+badgeClass+'">'+r.note+'</span></td></tr>';
    });
  }
  document.getElementById('histBody').innerHTML=body;
  document.getElementById('histModal').classList.add('active');
}
function closeModal(){document.getElementById('histModal').classList.remove('active');}
function doSearch(){
  var f=document.getElementById('srch').value.toUpperCase();
  var rows=document.getElementById('ctable').getElementsByTagName('tr');
  for(var i=1;i<rows.length;i++){
    var cells=rows[i].getElementsByTagName('td');
    var show=false;
    for(var c=0;c<2;c++){if(cells[c]&&cells[c].textContent.toUpperCase().indexOf(f)>-1)show=true;}
    rows[i].style.display=show?'':'none';
  }
}
)js";
  page += "</script></body></html>";
  server.send(200, "text/html", page);
}

// ─── LOGIN / LOGOUT / REMOVE / REDEEM / CSV ──────────────────
void handleLogin() {
  if (server.hasArg("user") && server.hasArg("pass")) {
    if (server.arg("user") == STAFF_EMAIL && server.arg("pass") == STAFF_PASS) staffLoggedIn = true;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleLogout() { staffLoggedIn = false; server.sendHeader("Location", "/"); server.send(303); }
void handleRemove() {
  if (!staffLoggedIn || !server.hasArg("idx")) { server.sendHeader("Location", "/"); server.send(303); return; }
  int idx = server.arg("idx").toInt();
  if (idx >= 0 && idx < customerCount) {
    for (int i = idx; i < customerCount - 1; i++) customers[i] = customers[i + 1];
    customerCount--;
    saveToFile();
  }
  server.sendHeader("Location", "/"); server.send(303);
}
void handleRedeem() {
  if (!staffLoggedIn || !server.hasArg("idx")) { server.sendHeader("Location", "/"); server.send(303); return; }
  int idx = server.arg("idx").toInt();
  if (idx >= 0 && idx < customerCount && customers[idx].totalPoints >= REDEEM_EVERY) {
    customers[idx].totalPoints -= REDEEM_EVERY;
    customers[idx].totalRedeemed++;
    addHistory(idx, -REDEEM_EVERY, "REDEEMED");
    saveToFile();
    lcd.clear(); lcd.print("REWARD REDEEMED!");
    lcd.setCursor(0, 1); lcd.print(customers[idx].name.substring(0, 16));
    delay(2500); lcd.clear(); lcd.print("SYSTEM READY");
  }
  server.sendHeader("Location", "/"); server.send(303);
}
void handleCSV() {
  if (!staffLoggedIn) { server.sendHeader("Location", "/"); server.send(303); return; }
  String csv = "ID,Name,TotalScans,CurrentPoints,TotalRedeemed\n";
  for (int i = 0; i < customerCount; i++) {
    csv += customers[i].id + "," + customers[i].name + "," + String(customers[i].totalScans) + "," + String(customers[i].totalPoints) + "," + String(customers[i].totalRedeemed) + "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=loyalty_" + getDate() + ".csv");
  server.send(200, "text/csv", csv);
}
void handleScanHTTP() {
  if (!server.hasArg("id") || !server.hasArg("name")) { server.send(400, "text/plain", "Missing id/name"); return; }
  String qrId = server.arg("id"); String qrName = server.arg("name");
  int idx = findCustomer(qrId);
  if (idx == -1) {
    if (customerCount >= MAX_CUSTOMERS) { server.send(500, "text/plain", "Full"); return; }
    customers[customerCount] = { qrId, qrName, 0, 0, 0, {}, 0 }; idx = customerCount++;
  }
  customers[idx].totalScans++;
  customers[idx].totalPoints += POINTS_PER_SCAN;
  addHistory(idx, POINTS_PER_SCAN, "EARNED");
  saveToFile();
  server.send(200, "text/plain", "OK");
}

// ─── SETUP ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  QRSerial.begin(9600, SERIAL_8N1, 16, 17);
  QRSerial.setTimeout(50); // Increased for better name reading

  esp_task_wdt_config_t twdt = { .timeout_ms = WDT_TIMEOUT * 1000, .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = true };
  esp_task_wdt_deinit(); esp_task_wdt_init(&twdt); esp_task_wdt_add(NULL);

  lcd.init(); lcd.backlight();
  lcd.print("CAFE LOYALTY");
  lcd.setCursor(0, 1); lcd.print("Starting...");

  if (!LittleFS.begin(true)) while (1);
  loadFromFile();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_pass);
  WiFi.begin(router_ssid, router_pass);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 15) { delay(500); retry++; }
  WiFi.setSleep(WIFI_PS_NONE);
  configTime(8 * 3600, 0, "pool.ntp.org");

  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", handleLogout);
  server.on("/remove", handleRemove);
  server.on("/redeem", handleRedeem);
  server.on("/download", handleCSV);
  server.on("/scan", handleScanHTTP);
  server.begin();

  lcd.clear(); lcd.print("SYSTEM READY");
  String ip = (WiFi.localIP().toString() == "0.0.0.0") ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  lcd.setCursor(0,1); lcd.print(ip);
}

// ─── LOOP ────────────────────────────────────────────────────
void loop() {
  esp_task_wdt_reset();
  server.handleClient();

  int tVal = touchRead(4);
  if (tVal < TOUCH_THRESHOLD && tVal > 1) {
    unsigned long ts = millis();
    while (touchRead(4) < TOUCH_THRESHOLD) {
      esp_task_wdt_reset();
      if (millis() - ts > 2000) {
        staffLoggedIn = true;
        lcd.clear(); lcd.print("TOUCH AUTH: OK");
        delay(2000); lcd.clear(); lcd.print("SYSTEM READY");
        break;
      }
      delay(50);
    }
  }

  if (QRSerial.available()) {
    // FIX: Read directly without clearing to prevent missing first letter
    String data = QRSerial.readStringUntil('\n');
    data.trim();
    if (data.length() < 1) return;
    lastScanMillis = millis();

    if (data.startsWith("Admin UK")) {
      if (data == "Admin UK 007" || data == "Admin UK 000") {
        systemEnabled = !systemEnabled;
        lcd.clear();
        if (systemEnabled) { lcd.backlight(); lcd.print("SYSTEM: ON"); }
        else               { lcd.noBacklight(); lcd.print("SYSTEM: OFF"); }
      }
      else if (data == "Admin UK 001") { staffLoggedIn = true; lcd.clear(); lcd.print("ADMIN LOGIN: OK"); }
      else if (data == "Admin UK 002") { lcd.clear(); lcd.print("IP:"); lcd.setCursor(0,1); lcd.print(WiFi.localIP()); }
      else if (data == "Admin UK 003") { lcd.clear(); lcd.print("DATE:"); lcd.setCursor(0,1); lcd.print(getDate()); }
      else if (data == "Admin UK 005") { lcd.clear(); lcd.print("TIME:"); lcd.setCursor(0,1); lcd.print(getTime()); }
      else if (data == "Admin UK 004") { lcd.clear(); lcd.print("HEAP:"); lcd.print(ESP.getFreeHeap() / 1024); lcd.print("KB"); }
      delay(3000); lcd.clear(); lcd.print(systemEnabled ? "SYSTEM READY" : "SYSTEM OFF");
      return;
    }

    if (!systemEnabled) {
      lcd.clear(); lcd.print("SYSTEM IS OFF"); delay(2000); lcd.clear(); lcd.print("SYSTEM OFF");
      return;
    }

    String qrName = data;
    String qrId = "NAME_" + qrName;
    int idx = findCustomer(qrId);

    if (idx == -1) {
      if (customerCount >= MAX_CUSTOMERS) { lcd.clear(); lcd.print("SYSTEM FULL!"); delay(2000); return; }
      customers[customerCount] = { qrId, qrName, 0, 0, 0, {}, 0 };
      idx = customerCount++;
    }

    customers[idx].totalScans++;
    customers[idx].totalPoints += POINTS_PER_SCAN;
    addHistory(idx, POINTS_PER_SCAN, "EARNED");
    saveToFile();

    lcd.clear(); lcd.print("HI, " + qrName.substring(0, 11) + "!");
    lcd.setCursor(0, 1);
    int pts = customers[idx].totalPoints;
    if (pts >= REDEEM_EVERY) lcd.print("REWARD READY! ★");
    else lcd.print("PTS:" + String(pts) + "/" + String(REDEEM_EVERY));
    
    delay(3000);
    lcd.clear(); lcd.print("SYSTEM READY");
    while (QRSerial.available()) QRSerial.read(); // Debounce after reading
  }
}