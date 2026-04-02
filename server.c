/*
 * ============================================================
 *   F2D — Hotel Table Reservation System
 *   Backend: C HTTP Server (Winsock2 — Windows compatible)
 *   Port: 8080
 *
 *   Compile on Windows (MinGW):
 *     gcc server.c -o server.exe -lws2_32
 *
 *   Compile on Linux/Mac:
 *     gcc server.c -o server
 * ============================================================
 */

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define close(s) closesocket(s)
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <signal.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* ── Constants ── */
#define PORT           8080
#define MAX_TABLES     20
#define MAX_USERS      50
#define MAX_RES        100
#define MAX_LOG        100
#define BUF            8192

/* ── Status values ── */
#define ST_AVAILABLE   "available"
#define ST_OCCUPIED    "occupied"
#define RS_PENDING     "pending"
#define RS_CONFIRMED   "confirmed"
#define RS_COMPLETED   "completed"
#define RS_REJECTED    "rejected"
#define PAY_UNPAID     "unpaid"
#define PAY_SUBMITTED  "submitted"
#define PAY_CONFIRMED  "paid"

/* ── Structures ── */
typedef struct {
    int  id;
    char name[64];
    int  seats;
    char status[16];
    char guest[64];
    char reserved_at[16];
    char note[128];
} Table;

typedef struct {
    int  id;
    char name[64];
    char email[128];
    char password[64];
    char role[16];
} User;

typedef struct {
    int  id;
    int  user_id;
    char user_name[64];
    int  table_id;
    char table_name[64];
    int  seats;
    char time[8];
    int  duration;
    char status[16];
    char payment[16];
    char created_at[32];
} Reservation;

typedef struct {
    char msg[256];
    char type[8];
    char ts[32];
} LogEntry;

/* ── Globals ── */
static Table       tables[MAX_TABLES];
static int         table_count = 0;
static User        users[MAX_USERS];
static int         user_count  = 0;
static Reservation reservations[MAX_RES];
static int         res_count   = 0;
static LogEntry    activity[MAX_LOG];
static int         log_count   = 0;
static int next_user_id = 3;
static int next_res_id  = 2;
static int next_tbl_id  = 6;

/* ── Utility ── */
static void get_ts(char *buf, int sz) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, sz, "%H:%M:%S", t);
}

static void add_log(const char *msg, const char *type) {
    int idx = log_count % MAX_LOG;
    strncpy(activity[idx].msg,  msg,  sizeof(activity[idx].msg)  - 1);
    strncpy(activity[idx].type, type, sizeof(activity[idx].type) - 1);
    get_ts(activity[idx].ts, sizeof(activity[idx].ts));
    log_count++;
}

static void json_str(char *out, int outsz, const char *in) {
    int i = 0, o = 0;
    out[o++] = '"';
    while (in[i] && o < outsz - 3) {
        if      (in[i] == '"')  { out[o++] = '\\'; out[o++] = '"';  }
        else if (in[i] == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else if (in[i] == '\n') { out[o++] = '\\'; out[o++] = 'n';  }
        else                    { out[o++] = in[i]; }
        i++;
    }
    out[o++] = '"';
    out[o]   = '\0';
}

static int json_get(const char *body, const char *key, char *val, int vsz) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(body, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < vsz - 1) { val[i++] = *p++; }
        val[i] = '\0';
        return 1;
    }
    int i = 0;
    while ((*p == '-' || isdigit((unsigned char)*p)) && i < vsz - 1) val[i++] = *p++;
    val[i] = '\0';
    return i > 0;
}

/* ── Seed data ── */
static void seed(void) {
    int cfg[][2] = {{1,2},{2,3},{3,4},{4,5},{5,8}};
    const char *notes[] = {
        "Intimate window seat","Garden view",
        "Central hall","Semi-private booth","Large family table"
    };
    for (int i = 0; i < 5; i++) {
        tables[i].id    = cfg[i][0];
        tables[i].seats = cfg[i][1];
        snprintf(tables[i].name, sizeof(tables[i].name), "Table %d", cfg[i][0]);
        strncpy(tables[i].status, ST_AVAILABLE, sizeof(tables[i].status)-1);
        tables[i].guest[0]       = '\0';
        tables[i].reserved_at[0] = '\0';
        strncpy(tables[i].note, notes[i], sizeof(tables[i].note)-1);
    }
    table_count = 5;

    strncpy(users[0].name,     "Demo Customer",      63);
    strncpy(users[0].email,    "customer@f2d.com",   127);
    strncpy(users[0].password, "demo123",            63);
    strncpy(users[0].role,     "customer",           15);
    users[0].id = 1;

    strncpy(users[1].name,     "Restaurant Manager", 63);
    strncpy(users[1].email,    "manager@f2d.com",    127);
    strncpy(users[1].password, "demo123",            63);
    strncpy(users[1].role,     "manager",            15);
    users[1].id = 2;
    user_count = 2;

    /* Demo reservation */
    reservations[0].id       = 1;
    reservations[0].user_id  = 1;
    reservations[0].table_id = 3;
    reservations[0].seats    = 4;
    reservations[0].duration = 2;
    strncpy(reservations[0].user_name,  "Demo Customer", 63);
    strncpy(reservations[0].table_name, "Table 3",       63);
    strncpy(reservations[0].time,       "19:00",         7);
    strncpy(reservations[0].status,     RS_PENDING,      15);
    strncpy(reservations[0].payment,    PAY_UNPAID,      15);
    strncpy(reservations[0].created_at, "Demo",          31);
    res_count = 1;

    strncpy(tables[2].status,      ST_OCCUPIED,     15);
    strncpy(tables[2].guest,       "Demo Customer", 63);
    strncpy(tables[2].reserved_at, "19:00",         15);

    add_log("System initialised. All tables set to available.", "INFO");
}

/* ── HTTP helpers ── */
static void send_response(int fd, int code, const char *ctype, const char *body) {
    char hdr[512];
    int blen = (int)strlen(body);
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        code, ctype, blen);
    send(fd, hdr,  (int)strlen(hdr), 0);
    send(fd, body, blen, 0);
}

static void send_json(int fd, int code, const char *body) {
    send_response(fd, code, "application/json", body);
}

static void send_options(int fd) {
    const char *r =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n\r\n";
    send(fd, r, (int)strlen(r), 0);
}

static void parse_request(const char *req, char *method, char *path) {
    sscanf(req, "%15s %255s", method, path);
}

static const char *get_body(const char *req) {
    const char *p = strstr(req, "\r\n\r\n");
    return p ? p + 4 : "";
}

/* ── JSON Builders ── */
static int build_tables_json(char *out, int outsz) {
    int n = snprintf(out, outsz, "[");
    for (int i = 0; i < table_count; i++) {
        Table *t = &tables[i];
        char nm[128], st[32], gu[128], ra[32], no[256];
        json_str(nm, sizeof(nm), t->name);
        json_str(st, sizeof(st), t->status);
        json_str(gu, sizeof(gu), t->guest);
        json_str(ra, sizeof(ra), t->reserved_at);
        json_str(no, sizeof(no), t->note);
        n += snprintf(out+n, outsz-n,
            "%s{\"id\":%d,\"name\":%s,\"seats\":%d,\"status\":%s,"
            "\"guest\":%s,\"reservedAt\":%s,\"note\":%s}",
            i ? "," : "", t->id, nm, t->seats, st, gu, ra, no);
    }
    n += snprintf(out+n, outsz-n, "]");
    return n;
}

static int build_res_json(char *out, int outsz) {
    int n = snprintf(out, outsz, "[");
    for (int i = 0; i < res_count; i++) {
        Reservation *r = &reservations[i];
        char un[128], tn[128], ti[32], st[32], py[32], ca[64];
        json_str(un, sizeof(un), r->user_name);
        json_str(tn, sizeof(tn), r->table_name);
        json_str(ti, sizeof(ti), r->time);
        json_str(st, sizeof(st), r->status);
        json_str(py, sizeof(py), r->payment);
        json_str(ca, sizeof(ca), r->created_at);
        n += snprintf(out+n, outsz-n,
            "%s{\"id\":%d,\"userId\":%d,\"userName\":%s,"
            "\"tableId\":%d,\"tableName\":%s,\"seats\":%d,"
            "\"time\":%s,\"duration\":%d,\"status\":%s,"
            "\"payment\":%s,\"createdAt\":%s}",
            i ? "," : "", r->id, r->user_id, un,
            r->table_id, tn, r->seats,
            ti, r->duration, st, py, ca);
    }
    n += snprintf(out+n, outsz-n, "]");
    return n;
}

static int build_log_json(char *out, int outsz) {
    int n = snprintf(out, outsz, "[");
    int count = (log_count < MAX_LOG) ? log_count : MAX_LOG;
    int start = (log_count - 1) % MAX_LOG;
    for (int i = 0; i < count; i++) {
        int idx = (start - i + MAX_LOG) % MAX_LOG;
        char msg[512], type[32], ts2[64];
        json_str(msg,  sizeof(msg),  activity[idx].msg);
        json_str(type, sizeof(type), activity[idx].type);
        json_str(ts2,  sizeof(ts2),  activity[idx].ts);
        n += snprintf(out+n, outsz-n,
            "%s{\"msg\":%s,\"type\":%s,\"time\":%s}",
            i ? "," : "", msg, type, ts2);
    }
    n += snprintf(out+n, outsz-n, "]");
    return n;
}

/* ── Handlers ── */
static void handle_get_tables(int fd) {
    char buf[BUF*2];
    build_tables_json(buf, sizeof(buf));
    send_json(fd, 200, buf);
}

static void handle_get_reservations(int fd) {
    char buf[BUF*2];
    build_res_json(buf, sizeof(buf));
    send_json(fd, 200, buf);
}

static void handle_get_log(int fd) {
    char buf[BUF*2];
    build_log_json(buf, sizeof(buf));
    send_json(fd, 200, buf);
}

static void handle_login(int fd, const char *body) {
    char email[128]={0}, pass[64]={0};
    json_get(body, "email",    email, sizeof(email));
    json_get(body, "password", pass,  sizeof(pass));

    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].email, email)==0 && strcmp(users[i].password, pass)==0) {
            char resp[512];
            char nm[128], em[256], rl[32];
            json_str(nm, sizeof(nm), users[i].name);
            json_str(em, sizeof(em), users[i].email);
            json_str(rl, sizeof(rl), users[i].role);
            snprintf(resp, sizeof(resp),
                "{\"ok\":true,\"user\":{\"id\":%d,\"name\":%s,\"email\":%s,\"role\":%s}}",
                users[i].id, nm, em, rl);
            char logmsg[128];
            snprintf(logmsg, sizeof(logmsg), "User logged in: %s (%s)", users[i].name, users[i].role);
            add_log(logmsg, "INFO");
            send_json(fd, 200, resp);
            return;
        }
    }
    send_json(fd, 401, "{\"ok\":false,\"error\":\"Invalid email or password\"}");
}

static void handle_signup(int fd, const char *body) {
    char name[64]={0}, email[128]={0}, pass[64]={0}, role[16]={0};
    json_get(body, "name",     name,  sizeof(name));
    json_get(body, "email",    email, sizeof(email));
    json_get(body, "password", pass,  sizeof(pass));
    json_get(body, "role",     role,  sizeof(role));
    if (!role[0]) strncpy(role, "customer", sizeof(role)-1);

    if (!name[0] || !email[0] || !pass[0]) {
        send_json(fd, 400, "{\"ok\":false,\"error\":\"All fields required\"}"); return;
    }
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].email, email)==0) {
            send_json(fd, 409, "{\"ok\":false,\"error\":\"Email already registered\"}"); return;
        }
    }
    if (user_count >= MAX_USERS) {
        send_json(fd, 500, "{\"ok\":false,\"error\":\"User limit reached\"}"); return;
    }

    User *u = &users[user_count++];
    u->id = next_user_id++;
    strncpy(u->name,     name,  sizeof(u->name)-1);
    strncpy(u->email,    email, sizeof(u->email)-1);
    strncpy(u->password, pass,  sizeof(u->password)-1);
    strncpy(u->role,     role,  sizeof(u->role)-1);

    char resp[512], nm[128], em[256], rl[32];
    json_str(nm, sizeof(nm), u->name);
    json_str(em, sizeof(em), u->email);
    json_str(rl, sizeof(rl), u->role);
    snprintf(resp, sizeof(resp),
        "{\"ok\":true,\"user\":{\"id\":%d,\"name\":%s,\"email\":%s,\"role\":%s}}",
        u->id, nm, em, rl);

    char logmsg[128];
    snprintf(logmsg, sizeof(logmsg), "New user registered: %s (%s)", name, role);
    add_log(logmsg, "INFO");
    send_json(fd, 200, resp);
}

static void handle_reserve(int fd, const char *body) {
    char uid_s[8]={0}, tid_s[8]={0}, seats_s[8]={0}, time_s[16]={0}, dur_s[8]={0};
    json_get(body, "userId",   uid_s,  sizeof(uid_s));
    json_get(body, "tableId",  tid_s,  sizeof(tid_s));
    json_get(body, "seats",    seats_s,sizeof(seats_s));
    json_get(body, "time",     time_s, sizeof(time_s));
    json_get(body, "duration", dur_s,  sizeof(dur_s));

    int uid = atoi(uid_s), tid = atoi(tid_s);
    int seats = atoi(seats_s), dur = atoi(dur_s);
    if (!dur) dur = 1;

    User  *u = NULL; for (int i=0;i<user_count;i++) if(users[i].id==uid){u=&users[i];break;}
    Table *t = NULL; for (int i=0;i<table_count;i++) if(tables[i].id==tid){t=&tables[i];break;}

    if (!u) { send_json(fd,400,"{\"ok\":false,\"error\":\"User not found\"}"); return; }
    if (!t) { send_json(fd,400,"{\"ok\":false,\"error\":\"Table not found\"}"); return; }
    if (strcmp(t->status, ST_AVAILABLE)!=0) { send_json(fd,400,"{\"ok\":false,\"error\":\"Table not available\"}"); return; }

    if (res_count >= MAX_RES) { send_json(fd,500,"{\"ok\":false,\"error\":\"Reservation limit reached\"}"); return; }

    Reservation *r = &reservations[res_count++];
    r->id       = next_res_id++;
    r->user_id  = uid;
    r->table_id = tid;
    r->seats    = seats;
    r->duration = dur;
    strncpy(r->user_name,  u->name,   sizeof(r->user_name)-1);
    strncpy(r->table_name, t->name,   sizeof(r->table_name)-1);
    strncpy(r->time,       time_s,    sizeof(r->time)-1);
    strncpy(r->status,     RS_PENDING, sizeof(r->status)-1);
    strncpy(r->payment,    PAY_UNPAID, sizeof(r->payment)-1);
    get_ts(r->created_at,  sizeof(r->created_at));

    strncpy(t->status,      ST_OCCUPIED, sizeof(t->status)-1);
    strncpy(t->guest,       u->name,     sizeof(t->guest)-1);
    strncpy(t->reserved_at, time_s,      sizeof(t->reserved_at)-1);

    char logmsg[200];
    snprintf(logmsg, sizeof(logmsg),
        "New reservation by %s at %s for %d seats at %s", u->name, t->name, seats, time_s);
    add_log(logmsg, "OK");
    send_json(fd, 200, "{\"ok\":true,\"message\":\"Reservation created\"}");
}

static void handle_payment(int fd, const char *body) {
    char rid_s[8]={0};
    json_get(body, "reservationId", rid_s, sizeof(rid_s));
    int rid = atoi(rid_s);

    for (int i = 0; i < res_count; i++) {
        if (reservations[i].id == rid) {
            strncpy(reservations[i].payment, PAY_SUBMITTED, sizeof(reservations[i].payment)-1);
            char logmsg[200];
            snprintf(logmsg, sizeof(logmsg),
                "Payment submitted for %s by %s",
                reservations[i].table_name, reservations[i].user_name);
            add_log(logmsg, "INFO");
            send_json(fd, 200, "{\"ok\":true,\"message\":\"Payment submitted\"}");
            return;
        }
    }
    send_json(fd, 400, "{\"ok\":false,\"error\":\"Reservation not found\"}");
}

static void handle_mgr_action(int fd, const char *body) {
    char rid_s[8]={0}, action[16]={0};
    json_get(body, "reservationId", rid_s,  sizeof(rid_s));
    json_get(body, "action",        action, sizeof(action));
    int rid = atoi(rid_s);

    Reservation *r = NULL;
    for (int i=0;i<res_count;i++) if(reservations[i].id==rid){r=&reservations[i];break;}
    if (!r) { send_json(fd,400,"{\"ok\":false,\"error\":\"Reservation not found\"}"); return; }

    Table *tbl = NULL;
    for (int i=0;i<table_count;i++) if(tables[i].id==r->table_id){tbl=&tables[i];break;}

    char logmsg[200];

    if (strcmp(action,"confirm")==0) {
        strncpy(r->status, RS_CONFIRMED, sizeof(r->status)-1);
        snprintf(logmsg,sizeof(logmsg),"Manager confirmed reservation for %s at %s",r->user_name,r->table_name);
        add_log(logmsg,"OK");
        send_json(fd,200,"{\"ok\":true,\"message\":\"Reservation confirmed\"}");

    } else if (strcmp(action,"reject")==0) {
        strncpy(r->status, RS_REJECTED, sizeof(r->status)-1);
        if(tbl){strncpy(tbl->status,ST_AVAILABLE,sizeof(tbl->status)-1);tbl->guest[0]='\0';tbl->reserved_at[0]='\0';}
        snprintf(logmsg,sizeof(logmsg),"Manager rejected reservation for %s",r->user_name);
        add_log(logmsg,"ERROR");
        send_json(fd,200,"{\"ok\":true,\"message\":\"Reservation rejected\"}");

    } else if (strcmp(action,"complete")==0) {
        strncpy(r->status,  RS_COMPLETED,  sizeof(r->status)-1);
        strncpy(r->payment, PAY_CONFIRMED, sizeof(r->payment)-1);
        if(tbl){strncpy(tbl->status,ST_AVAILABLE,sizeof(tbl->status)-1);tbl->guest[0]='\0';tbl->reserved_at[0]='\0';}
        snprintf(logmsg,sizeof(logmsg),"%s completed — table freed. Guest: %s",r->table_name,r->user_name);
        add_log(logmsg,"OK");
        send_json(fd,200,"{\"ok\":true,\"message\":\"Completed, table freed\"}");

    } else if (strcmp(action,"cancel")==0) {
        strncpy(r->status, RS_REJECTED, sizeof(r->status)-1);
        if(tbl){strncpy(tbl->status,ST_AVAILABLE,sizeof(tbl->status)-1);tbl->guest[0]='\0';tbl->reserved_at[0]='\0';}
        snprintf(logmsg,sizeof(logmsg),"Manager cancelled reservation for %s",r->user_name);
        add_log(logmsg,"INFO");
        send_json(fd,200,"{\"ok\":true,\"message\":\"Reservation cancelled\"}");

    } else {
        send_json(fd,400,"{\"ok\":false,\"error\":\"Unknown action\"}");
    }
}

static void handle_add_table(int fd, const char *body) {
    char name[64]={0}, seats_s[8]={0}, note[128]={0};
    json_get(body,"name",  name,    sizeof(name));
    json_get(body,"seats", seats_s, sizeof(seats_s));
    json_get(body,"note",  note,    sizeof(note));
    if (!name[0]){ send_json(fd,400,"{\"ok\":false,\"error\":\"Name required\"}"); return; }
    int seats = atoi(seats_s); if(!seats) seats=4;
    if (table_count>=MAX_TABLES){ send_json(fd,500,"{\"ok\":false,\"error\":\"Table limit reached\"}"); return; }
    Table *t = &tables[table_count++];
    t->id=next_tbl_id++; t->seats=seats;
    strncpy(t->name,   name, sizeof(t->name)-1);
    strncpy(t->note,   note, sizeof(t->note)-1);
    strncpy(t->status, ST_AVAILABLE, sizeof(t->status)-1);
    t->guest[0]='\0'; t->reserved_at[0]='\0';
    char logmsg[128];
    snprintf(logmsg,sizeof(logmsg),"New table added: %s (%d seats)",name,seats);
    add_log(logmsg,"INFO");
    send_json(fd,200,"{\"ok\":true,\"message\":\"Table added\"}");
}

static void handle_edit_table(int fd, const char *body) {
    char id_s[8]={0},name[64]={0},seats_s[8]={0},note[128]={0};
    json_get(body,"id",    id_s,    sizeof(id_s));
    json_get(body,"name",  name,    sizeof(name));
    json_get(body,"seats", seats_s, sizeof(seats_s));
    json_get(body,"note",  note,    sizeof(note));
    int id=atoi(id_s);
    for(int i=0;i<table_count;i++){
        if(tables[i].id==id){
            if(name[0])    strncpy(tables[i].name,  name,    sizeof(tables[i].name)-1);
            if(seats_s[0]) tables[i].seats=atoi(seats_s);
            strncpy(tables[i].note,note,sizeof(tables[i].note)-1);
            char logmsg[128];
            snprintf(logmsg,sizeof(logmsg),"Table updated: %s",tables[i].name);
            add_log(logmsg,"INFO");
            send_json(fd,200,"{\"ok\":true,\"message\":\"Table updated\"}");
            return;
        }
    }
    send_json(fd,400,"{\"ok\":false,\"error\":\"Table not found\"}");
}

static void handle_delete_table(int fd, const char *body) {
    char id_s[8]={0};
    json_get(body,"id",id_s,sizeof(id_s));
    int id=atoi(id_s);
    for(int i=0;i<table_count;i++){
        if(tables[i].id==id){
            if(strcmp(tables[i].status,ST_OCCUPIED)==0){
                send_json(fd,400,"{\"ok\":false,\"error\":\"Cannot delete occupied table\"}"); return;
            }
            char logmsg[64];
            snprintf(logmsg,sizeof(logmsg),"Table deleted: %s",tables[i].name);
            add_log(logmsg,"INFO");
            for(int j=i;j<table_count-1;j++) tables[j]=tables[j+1];
            table_count--;
            send_json(fd,200,"{\"ok\":true,\"message\":\"Table deleted\"}");
            return;
        }
    }
    send_json(fd,400,"{\"ok\":false,\"error\":\"Table not found\"}");
}

/* ── Read full HTTP request (headers + body) ── */
static int recv_full(SOCKET fd, char *buf, int bufsz) {
    int total = 0;
    /* Read until we have the full headers */
    while (total < bufsz - 1) {
        int n = recv(fd, buf + total, bufsz - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        /* Check if we have full headers */
        if (strstr(buf, "\r\n\r\n")) break;
    }
    buf[total] = '\0';

    /* Parse Content-Length and read remaining body if needed */
    const char *cl = strstr(buf, "Content-Length:");
    if (!cl) cl = strstr(buf, "content-length:");
    if (cl) {
        int content_len = atoi(cl + 15);
        const char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            int body_received = (int)(buf + total - body_start);
            /* Read remaining body bytes */
            while (body_received < content_len && total < bufsz - 1) {
                int n = recv(fd, buf + total, bufsz - 1 - total, 0);
                if (n <= 0) break;
                total += n;
                body_received += n;
                buf[total] = '\0';
            }
        }
    }
    return total;
}

/* ── Request Router ── */
static void handle_client(SOCKET fd) {
    char req[BUF*2]={0};
    int n = recv_full(fd, req, sizeof(req)-1);
    if(n<=0){ close(fd); return; }
    req[n]='\0';

    char method[16]={0}, path[256]={0};
    parse_request(req, method, path);

    if(strcmp(method,"OPTIONS")==0){ send_options(fd); close(fd); return; }

    const char *body = get_body(req);

    if(strcmp(method,"GET")==0){
        if     (strcmp(path,"/api/tables")==0)        handle_get_tables(fd);
        else if(strcmp(path,"/api/reservations")==0)  handle_get_reservations(fd);
        else if(strcmp(path,"/api/log")==0)            handle_get_log(fd);
        else send_json(fd,404,"{\"error\":\"Not found\"}");
    } else if(strcmp(method,"POST")==0){
        if     (strcmp(path,"/api/login")==0)          handle_login(fd,body);
        else if(strcmp(path,"/api/signup")==0)         handle_signup(fd,body);
        else if(strcmp(path,"/api/reserve")==0)        handle_reserve(fd,body);
        else if(strcmp(path,"/api/payment")==0)        handle_payment(fd,body);
        else if(strcmp(path,"/api/manager/action")==0) handle_mgr_action(fd,body);
        else if(strcmp(path,"/api/tables/add")==0)     handle_add_table(fd,body);
        else if(strcmp(path,"/api/tables/edit")==0)    handle_edit_table(fd,body);
        else if(strcmp(path,"/api/tables/delete")==0)  handle_delete_table(fd,body);
        else send_json(fd,404,"{\"error\":\"Not found\"}");
    } else {
        send_json(fd,405,"{\"error\":\"Method not allowed\"}");
    }

    close(fd);
}

/* ── Main ── */
int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){
        fprintf(stderr,"WSAStartup failed\n"); return 1;
    }
#endif

    seed();

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if(srv==INVALID_SOCKET){ perror("socket"); return 1; }

    int opt=1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))<0){ perror("bind"); return 1; }
    if(listen(srv,10)<0){ perror("listen"); return 1; }

    printf("F2D C Backend running on http://localhost:%d\n", PORT);
    fflush(stdout);

    while(1){
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        SOCKET fd = accept(srv,(struct sockaddr*)&cli,&cli_len);
        if(fd==INVALID_SOCKET) continue;
        handle_client(fd);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}