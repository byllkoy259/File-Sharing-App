#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "../common/protocol.h"

// --- Cấu hình kết nối ---
static char g_server_ip[64] = "127.0.0.1";
static int g_server_port = 8888;

// --- Struct lưu trạng thái ứng dụng ---
typedef struct {
    int sockfd;
    uint32_t session_id;
    char current_user[MAX_USERNAME];

    // Trạng thái điều hướng File
    uint32_t current_group_id;
    char current_group_name[MAX_GROUPNAME];
    char current_group_owner[MAX_USERNAME];
    char current_path[MAX_PATH];

    // Widget màn hình chi tiết nhóm (File Explorer)
    GtkWidget *lbl_current_path;
    GtkWidget *file_list_box;

    // Widget giao diện chung
    GtkWidget *window;
    GtkWidget *stack;           
    
    // Login widgets
    GtkWidget *login_entry_user;
    GtkWidget *login_entry_pass;
    GtkWidget *login_label_status;

    // Register widgets
    GtkWidget *reg_entry_user;
    GtkWidget *reg_entry_pass;
    GtkWidget *reg_label_status;
    
    // Widget màn hình chính
    GtkWidget *main_label;      
    GtkWidget *group_list_box; 
} AppState;

AppState app_state = { .sockfd = -1, .session_id = 0 };

// --- Forward Declarations ---
void refresh_group_list(AppState *state);
void on_create_group_clicked(GtkWidget *widget, gpointer data); // Khai báo trước để dùng lại
void on_refresh_clicked(GtkWidget *widget, gpointer data);
gboolean on_file_list_right_click(GtkWidget *widget, GdkEventButton *event, gpointer data); // Forward declaration

// --- Hàm tiện ích mạng ---
int connect_server() {
    if (app_state.sockfd != -1) return 0;

    struct sockaddr_in server_addr;
    app_state.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (app_state.sockfd == -1) return -1;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    if (inet_pton(AF_INET, g_server_ip, &server_addr.sin_addr) <= 0) {
        close(app_state.sockfd);
        app_state.sockfd = -1;
        return -1;
    }

    if (connect(app_state.sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(app_state.sockfd);
        app_state.sockfd = -1;
        return -1;
    }
    return 0;
}

// --- Logic xử lý Server (Groups) ---
void refresh_group_list(AppState *state) {
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(state->group_list_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (state->sockfd == -1) return;

    MessageHeader header;
    header.command = CMD_LIST_GROUPS;
    header.data_length = 0;
    header.session_id = state->session_id;

    if (send_message(state->sockfd, &header, NULL) < 0) {
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    void *data = NULL;
    if (recv_message(state->sockfd, &header, &data) < 0) {
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        GroupRecord *groups = (GroupRecord *)((char *)data + sizeof(Response));
        int count = (header.data_length - sizeof(Response)) / sizeof(GroupRecord);

        for (int i = 0; i < count; i++) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "ID: %u | %s (Owner: %s) - Members: %d", 
                     groups[i].group_id, groups[i].group_name, groups[i].owner, groups[i].member_count);
            
            GtkWidget *row_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(row_label), buffer);
            gtk_widget_set_halign(row_label, GTK_ALIGN_START);
            gtk_list_box_insert(GTK_LIST_BOX(state->group_list_box), row_label, -1);
            // Lưu dữ liệu group_id, group_name vào row để dùng khi click
            GtkWidget *row = gtk_widget_get_parent(row_label);
            g_object_set_data(G_OBJECT(row), "group_id", GINT_TO_POINTER(groups[i].group_id));
            g_object_set_data_full(G_OBJECT(row), "group_name", g_strdup(groups[i].group_name), g_free);
            g_object_set_data_full(G_OBJECT(row), "owner", g_strdup(groups[i].owner), g_free);
        }
        gtk_widget_show_all(state->group_list_box);
    }
    if (data) free(data);
}

// Cập nhật path: "docs" + "work" -> "docs/work"
void push_path(char *current, const char *segment) {
    if (strlen(current) == 0) strncpy(current, segment, MAX_PATH - 1);
    else {
        strncat(current, "/", MAX_PATH - strlen(current) - 1);
        strncat(current, segment, MAX_PATH - strlen(current) - 1);
    }
}

// Cập nhật path: "docs/work" -> "docs"
void pop_path(char *current) {
    char *last_slash = strrchr(current, '/');
    if (last_slash) *last_slash = '\0';
    else current[0] = '\0';
}

// Hàm lấy danh sách file từ server
void refresh_file_list(AppState *state) {
    // Xóa list cũ
    GList *children = gtk_container_get_children(GTK_CONTAINER(state->file_list_box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Cập nhật Label đường dẫn
    char path_display[1024];
    snprintf(path_display, sizeof(path_display), "<b>Group:</b> %s   |   <b>Path:</b> /%s", 
             state->current_group_name, state->current_path);
    gtk_label_set_markup(GTK_LABEL(state->lbl_current_path), path_display);
    gtk_label_set_xalign(GTK_LABEL(state->lbl_current_path), 0.0);

    if (state->sockfd == -1) return;

    // Gửi request
    PathRequest req;
    memset(&req, 0, sizeof(req));
    req.group_id = state->current_group_id;
    strncpy(req.path, state->current_path, sizeof(req.path) - 1);

    MessageHeader header = {CMD_LIST_FILES, sizeof(PathRequest), state->session_id};
    if (send_message(state->sockfd, &header, &req) < 0) {
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    void *data = NULL;
    if (recv_message(state->sockfd, &header, &data) < 0) {
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    // Hiển thị file
    if (header.command == RESP_SUCCESS && header.data_length >= sizeof(Response) + sizeof(ListResultHeader)) {
        ListResultHeader *lh = (ListResultHeader *)((char *)data + sizeof(Response));
        DirEntry *entries = (DirEntry *)((char *)data + sizeof(Response) + sizeof(ListResultHeader));

        for (uint32_t i = 0; i < lh->count; i++) {
            GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            GtkWidget *icon = gtk_label_new(entries[i].is_dir ? "📁" : "📄");
            
            char label_text[512];
            if (entries[i].is_dir) {
                snprintf(label_text, sizeof(label_text), "<b>%s</b>", entries[i].name);
            } else {
                time_t t = (time_t)entries[i].mtime;
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", localtime(&t));
                snprintf(label_text, sizeof(label_text), "<b>%s</b>\n<span size='small' color='gray'>%llu bytes | %s</span>", entries[i].name, (unsigned long long)entries[i].size, time_str);
            }
            
            GtkWidget *lbl = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(lbl), label_text);
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);
            
            gtk_box_pack_start(GTK_BOX(row_box), icon, FALSE, FALSE, 5);
            gtk_box_pack_start(GTK_BOX(row_box), lbl, TRUE, TRUE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(state->file_list_box), row_box, -1);

            // Lưu dữ liệu vào row để dùng khi click
            GtkWidget *row = gtk_widget_get_parent(row_box);
            g_object_set_data_full(G_OBJECT(row), "filename", g_strdup(entries[i].name), g_free);
            g_object_set_data(G_OBJECT(row), "is_dir", GINT_TO_POINTER(entries[i].is_dir));
        }
        gtk_widget_show_all(state->file_list_box);
    }
    if (data) free(data);
}

// --- Callbacks Auth (Login/Register) ---

// Điều hướng
void on_nav_to_login(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    gtk_label_set_text(GTK_LABEL(state->login_label_status), "");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "login_form");
}

void on_nav_to_register(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    gtk_label_set_text(GTK_LABEL(state->reg_label_status), "");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "register_form");
}

void on_nav_back(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "start_page");
}

// Xử lý sự kiện bấm nút Register
void on_register_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    const char *username = gtk_entry_get_text(GTK_ENTRY(state->reg_entry_user));
    const char *password = gtk_entry_get_text(GTK_ENTRY(state->reg_entry_pass));

    if (strlen(username) == 0 || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), "Please enter Username & Password!");
        return;
    }

    if (connect_server() < 0) {
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), "Connection failed!");
        return;
    }

    // Gửi gói tin Register
    AuthRequest auth;
    memset(&auth, 0, sizeof(auth));
    strncpy(auth.username, username, MAX_USERNAME - 1);
    strncpy(auth.password, password, MAX_PASSWORD - 1);

    MessageHeader header;
    header.command = CMD_REGISTER;
    header.data_length = sizeof(AuthRequest);
    header.session_id = 0;

    if (send_message(state->sockfd, &header, &auth) < 0) {
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), "Send failed!");
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    void *resp_data = NULL;
    if (recv_message(state->sockfd, &header, &resp_data) < 0) {
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), "Receive failed!");
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    Response *resp = (Response *)resp_data;
    if (header.command == RESP_SUCCESS) {
        // Đăng ký thành công -> Báo user đăng nhập
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), "✓ Registered! Please login now.");
    } else {
        // Đăng ký thất bại (ví dụ: trùng tên)
        char err_msg[300];
        snprintf(err_msg, sizeof(err_msg), "Register failed: %s", resp->message);
        gtk_label_set_text(GTK_LABEL(state->reg_label_status), err_msg);
    }
    if (resp_data) free(resp_data);
}

// Xử lý sự kiện bấm nút Login
void on_login_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    const char *username = gtk_entry_get_text(GTK_ENTRY(state->login_entry_user));
    const char *password = gtk_entry_get_text(GTK_ENTRY(state->login_entry_pass));

    if (strlen(username) == 0 || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(state->login_label_status), "Username/Password empty!");
        return;
    }

    if (connect_server() < 0) {
        gtk_label_set_text(GTK_LABEL(state->login_label_status), "Connection failed!");
        return;
    }

    AuthRequest auth;
    memset(&auth, 0, sizeof(auth));
    strncpy(auth.username, username, MAX_USERNAME - 1);
    strncpy(auth.password, password, MAX_PASSWORD - 1);

    MessageHeader header;
    header.command = CMD_LOGIN;
    header.data_length = sizeof(AuthRequest);
    header.session_id = 0;

    if (send_message(state->sockfd, &header, &auth) < 0) {
        gtk_label_set_text(GTK_LABEL(state->login_label_status), "Send failed!");
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    void *resp_data = NULL;
    if (recv_message(state->sockfd, &header, &resp_data) < 0) {
        gtk_label_set_text(GTK_LABEL(state->login_label_status), "Receive failed!");
        close(state->sockfd);
        state->sockfd = -1;
        return;
    }

    Response *resp = (Response *)resp_data;
    if (header.command == RESP_SUCCESS) {
        state->session_id = header.session_id;
        strcpy(state->current_user, username);
        
        char welcome[100];
        snprintf(welcome, sizeof(welcome), "User: %s", state->current_user);
        gtk_label_set_text(GTK_LABEL(state->main_label), welcome);
        
        refresh_group_list(state);
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "main_page");
    } else {
        char err_msg[300];
        snprintf(err_msg, sizeof(err_msg), "Login failed: %s", resp->message);
        gtk_label_set_text(GTK_LABEL(state->login_label_status), err_msg);
    }
    if (resp_data) free(resp_data);
}

// --- Callbacks Main Dashboard ---

void on_logout_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    MessageHeader header;
    header.command = CMD_LOGOUT;
    header.data_length = 0;
    header.session_id = state->session_id;
    send_message(state->sockfd, &header, NULL);

    state->session_id = 0;
    state->current_user[0] = '\0';
    
    gtk_entry_set_text(GTK_ENTRY(state->login_entry_pass), ""); 
    gtk_label_set_text(GTK_LABEL(state->login_label_status), "Logged out.");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "start_page");
}

void on_refresh_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    refresh_group_list((AppState *)data);
}

void on_create_group_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create Group",
                                                    GTK_WINDOW(state->window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Create", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_name), "Enter group name...");
    gtk_container_add(GTK_CONTAINER(content_area), entry_name);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *group_name = gtk_entry_get_text(GTK_ENTRY(entry_name));
        if (strlen(group_name) > 0) {
            MessageHeader header;
            header.command = CMD_CREATE_GROUP;
            header.data_length = strlen(group_name) + 1;
            header.session_id = state->session_id;
            
            char name_buf[MAX_GROUPNAME];
            strncpy(name_buf, group_name, sizeof(name_buf));

            if (send_message(state->sockfd, &header, name_buf) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data); 
                if (resp_data) free(resp_data);
                refresh_group_list(state);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

// Khi click vào một nhóm -> Vào màn hình File
void on_group_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    AppState *state = (AppState *)data;
    
    // Lấy ID nhóm từ row (đã gắn ở Bước 3)
    uint32_t group_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "group_id"));
    char *group_name = (char *)g_object_get_data(G_OBJECT(row), "group_name");
    char *owner = (char *)g_object_get_data(G_OBJECT(row), "owner");

    state->current_group_id = group_id;
    strncpy(state->current_group_name, group_name ? group_name : "Unknown", MAX_GROUPNAME);
    strncpy(state->current_group_owner, owner ? owner : "", MAX_USERNAME);
    state->current_path[0] = '\0'; // Reset về root

    // Chuyển trang
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "group_page");
    refresh_file_list(state);
}

// Khi click vào file/folder
void on_file_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    AppState *state = (AppState *)data;

    int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
    char *filename = (char *)g_object_get_data(G_OBJECT(row), "filename");

    if (is_dir) {
        push_path(state->current_path, filename);
        refresh_file_list(state);
    } else {
        // Tạm thời chỉ hiện thông báo
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Selected file: %s", filename);
        gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
    }
}

// Nút Back
void on_file_back_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    if (strlen(state->current_path) == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "main_page"); // Về dashboard
    } else {
        pop_path(state->current_path); // Lên thư mục cha
        refresh_file_list(state);
    }
}

// --- Các hàm xử lý Nhóm & Thành viên mới ---

void on_join_group_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Join Group", GTK_WINDOW(state->window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Join", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter Group ID");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        uint32_t group_id = (uint32_t)atoi(text);
        if (group_id > 0) {
            MessageHeader header = {CMD_REQUEST_JOIN_GROUP, sizeof(uint32_t), state->session_id};
            if (send_message(state->sockfd, &header, &group_id) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                if (header.command == RESP_SUCCESS) {
                    GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Request sent successfully!");
                    gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
                } else {
                    Response *r = (Response *)resp_data;
                    GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Error: %s", r->message);
                    gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
                }
                if (resp_data) free(resp_data);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void on_decide_request(GtkWidget *btn, gpointer data) {
    AppState *state = (AppState *)data;
    uint32_t req_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "req_id"));
    uint32_t approve = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "approve"));
    GtkWidget *dialog = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dialog"));

    DecisionPayload payload = {req_id, approve};
    MessageHeader header = {CMD_DECIDE_JOIN_REQUEST, sizeof(DecisionPayload), state->session_id};
    
    if (send_message(state->sockfd, &header, &payload) >= 0) {
        void *resp_data = NULL;
        recv_message(state->sockfd, &header, &resp_data);
        if (resp_data) free(resp_data);
        
        // Đóng dialog cũ để user mở lại refresh list (đơn giản hóa)
        gtk_widget_destroy(dialog);
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Processed request %u.", req_id);
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
    }
}

void on_manage_requests_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    uint32_t group_id = 0; // 0 = all groups

    MessageHeader header = {CMD_LIST_JOIN_REQUESTS, sizeof(uint32_t), state->session_id};
    if (send_message(state->sockfd, &header, &group_id) < 0) return;

    void *resp_data = NULL;
    if (recv_message(state->sockfd, &header, &resp_data) < 0) return;

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        int count = (header.data_length - sizeof(Response)) / sizeof(JoinRequestInfo);
        JoinRequestInfo *reqs = (JoinRequestInfo *)((char *)resp_data + sizeof(Response));

        GtkWidget *dialog = gtk_dialog_new_with_buttons("Pending Join Requests", GTK_WINDOW(state->window),
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "Close", GTK_RESPONSE_CLOSE, NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *list = gtk_list_box_new();
        gtk_container_add(GTK_CONTAINER(content), list);

        for (int i = 0; i < count; i++) {
            if (reqs[i].status == REQUEST_STATUS_PENDING) {
                GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                char buf[256];
                snprintf(buf, sizeof(buf), "User <b>%s</b> -> Group ID <b>%u</b>", reqs[i].username, reqs[i].group_id);
                GtkWidget *lbl = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(lbl), buf);

                GtkWidget *btn_ok = gtk_button_new_with_label("Approve");
                GtkWidget *btn_no = gtk_button_new_with_label("Reject");

                g_object_set_data(G_OBJECT(btn_ok), "req_id", GUINT_TO_POINTER(reqs[i].request_id));
                g_object_set_data(G_OBJECT(btn_ok), "approve", GUINT_TO_POINTER(1));
                g_object_set_data(G_OBJECT(btn_ok), "dialog", dialog);
                g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_decide_request), state);

                g_object_set_data(G_OBJECT(btn_no), "req_id", GUINT_TO_POINTER(reqs[i].request_id));
                g_object_set_data(G_OBJECT(btn_no), "approve", GUINT_TO_POINTER(0));
                g_object_set_data(G_OBJECT(btn_no), "dialog", dialog);
                g_signal_connect(btn_no, "clicked", G_CALLBACK(on_decide_request), state);

                gtk_box_pack_start(GTK_BOX(row_box), lbl, TRUE, TRUE, 5);
                gtk_box_pack_start(GTK_BOX(row_box), btn_ok, FALSE, FALSE, 2);
                gtk_box_pack_start(GTK_BOX(row_box), btn_no, FALSE, FALSE, 2);
                gtk_list_box_insert(GTK_LIST_BOX(list), row_box, -1);
            }
        }
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "No pending requests or failed to fetch.");
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
    }
    if (resp_data) free(resp_data);
}

void on_add_member_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Member", GTK_WINDOW(state->window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Add", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter Username");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *username = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(username) > 0) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%u:%s", state->current_group_id, username);
            MessageHeader header = {CMD_ADD_MEMBER, strlen(buffer) + 1, state->session_id};
            
            if (send_message(state->sockfd, &header, buffer) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                if (resp_data) free(resp_data);
                // Thông báo thành công đơn giản
                GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Add member command sent.");
                gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void on_remove_member_clicked(GtkWidget *btn, gpointer data) {
    AppState *state = (AppState *)data;
    char *username = (char *)g_object_get_data(G_OBJECT(btn), "username");
    GtkWidget *dialog = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dialog"));

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%u:%s", state->current_group_id, username);
    MessageHeader header = {CMD_REMOVE_MEMBER, strlen(buffer) + 1, state->session_id};

    if (send_message(state->sockfd, &header, buffer) >= 0) {
        void *resp_data = NULL;
        recv_message(state->sockfd, &header, &resp_data);
        if (resp_data) free(resp_data);
        
        gtk_widget_destroy(dialog); // Đóng dialog để refresh
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Removed %s.", username);
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
    }
}

void on_group_members_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    MessageHeader header = {CMD_LIST_GROUP_MEMBERS, sizeof(uint32_t), state->session_id};
    if (send_message(state->sockfd, &header, &state->current_group_id) < 0) return;

    void *resp_data = NULL;
    if (recv_message(state->sockfd, &header, &resp_data) < 0) return;

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        int count = (header.data_length - sizeof(Response)) / sizeof(MemberRecord);
        MemberRecord *mems = (MemberRecord *)((char *)resp_data + sizeof(Response));

        GtkWidget *dialog = gtk_dialog_new_with_buttons("Group Members", GTK_WINDOW(state->window),
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "Close", GTK_RESPONSE_CLOSE, NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *list = gtk_list_box_new();
        gtk_container_add(GTK_CONTAINER(content), list);

        int is_owner = (strcmp(state->current_user, state->current_group_owner) == 0);

        for (int i = 0; i < count; i++) {
            GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (%s)", mems[i].username, mems[i].is_admin ? "Owner" : "Member");
            GtkWidget *lbl = gtk_label_new(buf);
            gtk_box_pack_start(GTK_BOX(row_box), lbl, TRUE, TRUE, 5);

            if (is_owner && strcmp(mems[i].username, state->current_user) != 0) {
                GtkWidget *btn_rm = gtk_button_new_with_label("Remove");
                g_object_set_data_full(G_OBJECT(btn_rm), "username", g_strdup(mems[i].username), g_free);
                g_object_set_data(G_OBJECT(btn_rm), "dialog", dialog);
                g_signal_connect(btn_rm, "clicked", G_CALLBACK(on_remove_member_clicked), state);
                gtk_box_pack_start(GTK_BOX(row_box), btn_rm, FALSE, FALSE, 2);
            }
            gtk_list_box_insert(GTK_LIST_BOX(list), row_box, -1);
        }

        if (is_owner) {
            GtkWidget *btn_add = gtk_button_new_with_label("Add Member");
            g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_member_clicked), state);
            gtk_container_add(GTK_CONTAINER(content), btn_add);
        }

        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    if (resp_data) free(resp_data);
}

void on_invite_user_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Invite User", GTK_WINDOW(state->window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Invite", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter Username");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *username = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(username) > 0) {
            GroupUserPayload payload;
            payload.group_id = state->current_group_id;
            strncpy(payload.username, username, MAX_USERNAME - 1);

            MessageHeader header = {CMD_INVITE_TO_GROUP, sizeof(GroupUserPayload), state->session_id};
            if (send_message(state->sockfd, &header, &payload) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                Response *r = (Response *)resp_data;
                GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        (header.command == RESP_SUCCESS) ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                                                        GTK_BUTTONS_OK, "%s", r->message);
                gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
                if (resp_data) free(resp_data);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void on_decide_invitation(GtkWidget *btn, gpointer data) {
    AppState *state = (AppState *)data;
    uint32_t invite_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "invite_id"));
    uint32_t approve = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "approve"));
    GtkWidget *dialog = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dialog"));

    DecisionPayload payload = {invite_id, approve};
    MessageHeader header = {CMD_DECIDE_INVITATION, sizeof(DecisionPayload), state->session_id};
    
    if (send_message(state->sockfd, &header, &payload) >= 0) {
        void *resp_data = NULL;
        recv_message(state->sockfd, &header, &resp_data);
        if (resp_data) free(resp_data);
        
        gtk_widget_destroy(dialog);
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Processed invitation.");
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
    }
}

void on_my_invitations_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    MessageHeader header = {CMD_LIST_INVITATIONS, 0, state->session_id};
    if (send_message(state->sockfd, &header, NULL) < 0) return;

    void *resp_data = NULL;
    if (recv_message(state->sockfd, &header, &resp_data) < 0) return;

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        int count = (header.data_length - sizeof(Response)) / sizeof(GroupInviteInfo);
        GroupInviteInfo *invites = (GroupInviteInfo *)((char *)resp_data + sizeof(Response));

        GtkWidget *dialog = gtk_dialog_new_with_buttons("My Invitations", GTK_WINDOW(state->window),
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "Close", GTK_RESPONSE_CLOSE, NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *list = gtk_list_box_new();
        gtk_container_add(GTK_CONTAINER(content), list);

        for (int i = 0; i < count; i++) {
            if (invites[i].status == REQUEST_STATUS_PENDING) {
                GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                char buf[256];
                snprintf(buf, sizeof(buf), "Group ID <b>%u</b> (Owner: %s)", invites[i].group_id, invites[i].owner);
                GtkWidget *lbl = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(lbl), buf);

                GtkWidget *btn_ok = gtk_button_new_with_label("Accept");
                GtkWidget *btn_no = gtk_button_new_with_label("Decline");

                g_object_set_data(G_OBJECT(btn_ok), "invite_id", GUINT_TO_POINTER(invites[i].invite_id));
                g_object_set_data(G_OBJECT(btn_ok), "approve", GUINT_TO_POINTER(1));
                g_object_set_data(G_OBJECT(btn_ok), "dialog", dialog);
                g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_decide_invitation), state);

                g_object_set_data(G_OBJECT(btn_no), "invite_id", GUINT_TO_POINTER(invites[i].invite_id));
                g_object_set_data(G_OBJECT(btn_no), "approve", GUINT_TO_POINTER(0));
                g_object_set_data(G_OBJECT(btn_no), "dialog", dialog);
                g_signal_connect(btn_no, "clicked", G_CALLBACK(on_decide_invitation), state);

                gtk_box_pack_start(GTK_BOX(row_box), lbl, TRUE, TRUE, 5);
                gtk_box_pack_start(GTK_BOX(row_box), btn_ok, FALSE, FALSE, 2);
                gtk_box_pack_start(GTK_BOX(row_box), btn_no, FALSE, FALSE, 2);
                gtk_list_box_insert(GTK_LIST_BOX(list), row_box, -1);
            }
        }
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "No pending invitations.");
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
    }
    if (resp_data) free(resp_data);
}

void on_leave_group_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to leave this group?");
    if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES) {
        MessageHeader header = {CMD_LEAVE_GROUP, sizeof(uint32_t), state->session_id};
        if (send_message(state->sockfd, &header, &state->current_group_id) >= 0) {
            void *resp_data = NULL;
            recv_message(state->sockfd, &header, &resp_data);
            if (resp_data) free(resp_data);
            
            gtk_widget_destroy(confirm);
            // Quay về trang chính
            gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "main_page");
            refresh_group_list(state);
            return;
        }
    }
    gtk_widget_destroy(confirm);
}

void on_transfer_owner_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Transfer Ownership", GTK_WINDOW(state->window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Transfer", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "New Owner Username");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_owner = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(new_owner) > 0) {
            TransferOwnerPayload payload;
            payload.group_id = state->current_group_id;
            strncpy(payload.new_owner, new_owner, MAX_USERNAME - 1);

            MessageHeader header = {CMD_TRANSFER_GROUP_OWNER, sizeof(TransferOwnerPayload), state->session_id};
            if (send_message(state->sockfd, &header, &payload) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                Response *r = (Response *)resp_data;
                
                GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        (header.command == RESP_SUCCESS) ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                                                        GTK_BUTTONS_OK, "%s", r->message);
                gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
                if (resp_data) free(resp_data);
                
                // Nếu thành công, cập nhật lại UI (có thể mất quyền owner)
                if (header.command == RESP_SUCCESS) {
                     strncpy(state->current_group_owner, new_owner, MAX_USERNAME);
                }
            }
        }
    }
    gtk_widget_destroy(dialog);
}

// --- Xây dựng giao diện ---

GtkWidget* create_start_page(AppState *state) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_title), "<span size='x-large' weight='bold'>File Sharing App</span>");

    GtkWidget *btn_login = gtk_button_new_with_label("Login");
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_nav_to_login), state);
    gtk_widget_set_size_request(btn_login, 150, 40);
    
    GtkWidget *btn_register = gtk_button_new_with_label("Register");
    g_signal_connect(btn_register, "clicked", G_CALLBACK(on_nav_to_register), state);
    gtk_widget_set_size_request(btn_register, 150, 40);

    gtk_box_pack_start(GTK_BOX(box), lbl_title, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(box), btn_login, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), btn_register, FALSE, FALSE, 5);

    return box;
}

GtkWidget* create_login_form(AppState *state) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_title), "<span size='large' weight='bold'>Login</span>");

    state->login_entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->login_entry_user), "Username");

    state->login_entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->login_entry_pass), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(state->login_entry_pass), FALSE);

    GtkWidget *btn_login = gtk_button_new_with_label("Login");
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login_clicked), state);

    GtkWidget *btn_back = gtk_button_new_with_label("Back");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_nav_back), state);

    state->login_label_status = gtk_label_new("");

    gtk_box_pack_start(GTK_BOX(box), lbl_title, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(box), state->login_entry_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), state->login_entry_pass, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_login, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), btn_back, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), state->login_label_status, FALSE, FALSE, 0);

    return box;
}

GtkWidget* create_register_form(AppState *state) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *lbl_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_title), "<span size='large' weight='bold'>Register</span>");

    state->reg_entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->reg_entry_user), "Username");

    state->reg_entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->reg_entry_pass), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(state->reg_entry_pass), FALSE);

    GtkWidget *btn_reg = gtk_button_new_with_label("Register");
    g_signal_connect(btn_reg, "clicked", G_CALLBACK(on_register_clicked), state);

    GtkWidget *btn_back = gtk_button_new_with_label("Back");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_nav_back), state);

    state->reg_label_status = gtk_label_new("");

    gtk_box_pack_start(GTK_BOX(box), lbl_title, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(box), state->reg_entry_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), state->reg_entry_pass, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_reg, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), btn_back, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), state->reg_label_status, FALSE, FALSE, 0);

    return box;
}

GtkWidget* create_main_page(AppState *state) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    // Top Bar
    GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    state->main_label = gtk_label_new("User: ???");
    GtkWidget *btn_logout = gtk_button_new_with_label("Logout");
    g_signal_connect(btn_logout, "clicked", G_CALLBACK(on_logout_clicked), state);
    
    gtk_box_pack_start(GTK_BOX(top_bar), state->main_label, TRUE, TRUE, 10); 
    gtk_box_pack_end(GTK_BOX(top_bar), btn_logout, FALSE, FALSE, 10);

    // Group List
    GtkWidget *lbl_groups = gtk_label_new("<b>Your Groups:</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_groups), TRUE);
    gtk_widget_set_halign(lbl_groups, GTK_ALIGN_START);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    
    state->group_list_box = gtk_list_box_new();
    g_signal_connect(state->group_list_box, "row-activated", G_CALLBACK(on_group_row_activated), state);
    gtk_container_add(GTK_CONTAINER(scrolled_window), state->group_list_box);

    // Bottom Bar
    GtkWidget *bottom_bar = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bottom_bar), GTK_BUTTONBOX_CENTER);
    
    GtkWidget *btn_create = gtk_button_new_with_label("Create New Group");
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_create_group_clicked), state);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh List");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), state);

    GtkWidget *btn_join = gtk_button_new_with_label("Join Group");
    g_signal_connect(btn_join, "clicked", G_CALLBACK(on_join_group_clicked), state);

    GtkWidget *btn_reqs = gtk_button_new_with_label("Requests");
    g_signal_connect(btn_reqs, "clicked", G_CALLBACK(on_manage_requests_clicked), state);

    GtkWidget *btn_invites = gtk_button_new_with_label("Invitations");
    g_signal_connect(btn_invites, "clicked", G_CALLBACK(on_my_invitations_clicked), state);

    gtk_container_add(GTK_CONTAINER(bottom_bar), btn_create);
    gtk_container_add(GTK_CONTAINER(bottom_bar), btn_join);
    gtk_container_add(GTK_CONTAINER(bottom_bar), btn_reqs);
    gtk_container_add(GTK_CONTAINER(bottom_bar), btn_invites);
    gtk_container_add(GTK_CONTAINER(bottom_bar), btn_refresh);

    gtk_box_pack_start(GTK_BOX(vbox), top_bar, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_groups, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), bottom_bar, FALSE, FALSE, 10);

    return vbox;
}

// Hàm lấy tên file từ đường dẫn đầy đủ
const char *get_filename(const char *path) {
    const char *f = strrchr(path, '/');
    return f ? f + 1 : path;
}

// Xử lý sự kiện bấm nút Upload
void on_upload_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;

    // 1. Mở hộp thoại chọn file (Native File Chooser của Linux)
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select File to Upload",
                                                    GTK_WINDOW(state->window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *local_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // 2. Lấy thông tin file
        struct stat st;
        if (stat(local_path, &st) == 0 && S_ISREG(st.st_mode)) {
            uint64_t filesize = (uint64_t)st.st_size;
            const char *filename = get_filename(local_path);

            // 3. Gửi lệnh Upload Init (CMD_UPLOAD_FILE)
            UploadInitPayload init;
            init.group_id = state->current_group_id;
            init.file_size = filesize;
            
            // Xử lý path remote (ghép thư mục hiện tại + tên file)
            char remote_full_path[MAX_PATH * 2]; // Tăng kích thước đệm tạm để tránh cảnh báo truncation
            if (strlen(state->current_path) == 0) {
                strncpy(remote_full_path, filename, sizeof(remote_full_path) - 1);
            } else {
                snprintf(remote_full_path, sizeof(remote_full_path), "%s/%s", state->current_path, filename);
            }
            strncpy(init.remote_path, remote_full_path, MAX_PATH - 1);
            init.remote_path[MAX_PATH - 1] = '\0'; // Đảm bảo luôn có ký tự kết thúc chuỗi

            MessageHeader header;
            header.command = CMD_UPLOAD_FILE;
            header.data_length = sizeof(UploadInitPayload);
            header.session_id = state->session_id;

            // Gửi header init
            send_message(state->sockfd, &header, &init);

            // Chờ server xác nhận "Ready"
            void *resp_data = NULL;
            recv_message(state->sockfd, &header, &resp_data);
            if (resp_data) free(resp_data);

            if (header.command == RESP_SUCCESS) {
                // 4. Bắt đầu gửi file theo Chunk (4KB)
                FILE *fp = fopen(local_path, "rb");
                if (fp) {
                    uint8_t buf[FILE_CHUNK_SIZE];
                    size_t n;
                    uint64_t offset = 0;
                    
                    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                        // Đóng gói chunk
                        uint32_t payload_len = sizeof(FileChunkHeader) + n;
                        uint8_t *payload = malloc(payload_len);
                        
                        FileChunkHeader ch;
                        ch.phase = FILE_PHASE_CHUNK;
                        ch.chunk_size = n;
                        ch.offset = offset;
                        
                        memcpy(payload, &ch, sizeof(ch));
                        memcpy(payload + sizeof(ch), buf, n);

                        MessageHeader ch_header = {CMD_UPLOAD_FILE, payload_len, state->session_id};
                        send_message(state->sockfd, &ch_header, payload);
                        
                        free(payload);
                        offset += n;
                    }
                    fclose(fp);

                    // 5. Gửi kết thúc (END)
                    FileChunkHeader end_ch = {FILE_PHASE_END, 0, offset};
                    MessageHeader end_header = {CMD_UPLOAD_FILE, sizeof(end_ch), state->session_id};
                    send_message(state->sockfd, &end_header, &end_ch);

                    // Nhận phản hồi cuối cùng
                    recv_message(state->sockfd, &header, &resp_data);
                    if (header.command == RESP_SUCCESS) {
                        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Upload Successful!");
                        gtk_dialog_run(GTK_DIALOG(msg));
                        gtk_widget_destroy(msg);
                        refresh_file_list(state); // Tải lại danh sách file ngay
                    }
                    if (resp_data) free(resp_data);
                }
            }
        }
        g_free(local_path);
    }
    gtk_widget_destroy(dialog);
}

// --- File Operations (Download, Mkdir, Context Menu) ---

void on_download_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(state->file_list_box));
    if (!row) {
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Please select a file to download.");
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
        return;
    }

    char *filename = (char *)g_object_get_data(G_OBJECT(row), "filename");
    int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));

    if (is_dir) {
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Cannot download a directory directly.");
        gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save File", GTK_WINDOW(state->window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), filename);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *local_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        DownloadRequestPayload req;
        req.group_id = state->current_group_id;
        
        char remote_full_path[MAX_PATH * 2];
        if (strlen(state->current_path) == 0) snprintf(remote_full_path, sizeof(remote_full_path), "%s", filename);
        else snprintf(remote_full_path, sizeof(remote_full_path), "%s/%s", state->current_path, filename);
        
        strncpy(req.remote_path, remote_full_path, MAX_PATH - 1);

        MessageHeader header = {CMD_DOWNLOAD_FILE, sizeof(DownloadRequestPayload), state->session_id};
        if (send_message(state->sockfd, &header, &req) >= 0) {
            void *resp_data = NULL;
            recv_message(state->sockfd, &header, &resp_data);
            
            if (header.command == RESP_SUCCESS) {
                FILE *fp = fopen(local_path, "wb");
                if (fp) {
                    while (1) {
                        void *chunk_data = NULL;
                        if (recv_message(state->sockfd, &header, &chunk_data) < 0) break;
                        
                        FileChunkHeader *ch = (FileChunkHeader *)chunk_data;
                        if (ch->phase == FILE_PHASE_END) {
                            free(chunk_data); break;
                        }
                        fwrite((uint8_t*)chunk_data + sizeof(FileChunkHeader), 1, ch->chunk_size, fp);
                        free(chunk_data);
                    }
                    fclose(fp);
                    GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Download complete!");
                    gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
                }
            } else {
                Response *r = (Response *)resp_data;
                GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Error: %s", r->message);
                gtk_dialog_run(GTK_DIALOG(msg)); gtk_widget_destroy(msg);
            }
            if (resp_data) free(resp_data);
        }
        g_free(local_path);
    }
    gtk_widget_destroy(dialog);
}

void on_mkdir_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppState *state = (AppState *)data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("New Folder", GTK_WINDOW(state->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Create", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Folder Name");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(name) > 0) {
            PathRequest req;
            req.group_id = state->current_group_id;
            
            char full_path[MAX_PATH * 2];
            if (strlen(state->current_path) == 0) snprintf(full_path, sizeof(full_path), "%s", name);
            else snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, name);
            strncpy(req.path, full_path, MAX_PATH - 1);

            MessageHeader header = {CMD_MKDIR, sizeof(PathRequest), state->session_id};
            if (send_message(state->sockfd, &header, &req) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                if (resp_data) free(resp_data);
                refresh_file_list(state);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

// --- Context Menu Actions ---

// Helper: Thực hiện xóa file/folder
void action_delete_file(AppState *state, const char *filename, int is_dir) {
    GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Delete '%s'?", filename);
    if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES) {
        PathRequest req;
        req.group_id = state->current_group_id;
        
        char full_path[MAX_PATH * 2];
        if (strlen(state->current_path) == 0) snprintf(full_path, sizeof(full_path), "%s", filename);
        else snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, filename);
        strncpy(req.path, full_path, MAX_PATH - 1);

        MessageHeader header = {is_dir ? CMD_RMDIR : CMD_DELETE_FILE, sizeof(PathRequest), state->session_id};
        if (send_message(state->sockfd, &header, &req) >= 0) {
            void *resp_data = NULL;
            recv_message(state->sockfd, &header, &resp_data);
            if (resp_data) free(resp_data);
            refresh_file_list(state);
        }
    }
    gtk_widget_destroy(confirm);
}

// Helper: Thực hiện đổi tên
void action_rename_file(AppState *state, const char *filename, int is_dir) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Rename", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Rename", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), filename);
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(new_name) > 0 && strcmp(new_name, filename) != 0) {
            MoveRequest req;
            req.group_id = state->current_group_id;
            
            char src_path[MAX_PATH * 2], dst_path[MAX_PATH * 2];
            if (strlen(state->current_path) == 0) {
                snprintf(src_path, sizeof(src_path), "%s", filename);
                snprintf(dst_path, sizeof(dst_path), "%s", new_name);
            } else {
                snprintf(src_path, sizeof(src_path), "%s/%s", state->current_path, filename);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", state->current_path, new_name);
            }
            strncpy(req.src, src_path, MAX_PATH - 1);
            strncpy(req.dst, dst_path, MAX_PATH - 1);

            MessageHeader header = {is_dir ? CMD_RENAME_DIR : CMD_RENAME_FILE, sizeof(MoveRequest), state->session_id};
            if (send_message(state->sockfd, &header, &req) >= 0) {
                void *resp_data = NULL;
                recv_message(state->sockfd, &header, &resp_data);
                if (resp_data) free(resp_data);
                refresh_file_list(state);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

// Helper: Thực hiện di chuyển
void action_move_file(AppState *state, const char *filename, int is_dir) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Move to...", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Move", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Destination Path (e.g., folder/subfolder)");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *dest_dir = gtk_entry_get_text(GTK_ENTRY(entry));
        MoveRequest req;
        req.group_id = state->current_group_id;
        
        char src_path[MAX_PATH * 2], dst_path[MAX_PATH * 2];
        if (strlen(state->current_path) == 0) snprintf(src_path, sizeof(src_path), "%s", filename);
        else snprintf(src_path, sizeof(src_path), "%s/%s", state->current_path, filename);
        
        // Destination logic: dest_dir + filename
        if (strlen(dest_dir) == 0 || strcmp(dest_dir, ".") == 0) snprintf(dst_path, sizeof(dst_path), "%s", filename);
        else snprintf(dst_path, sizeof(dst_path), "%s/%s", dest_dir, filename);

        strncpy(req.src, src_path, MAX_PATH - 1);
        strncpy(req.dst, dst_path, MAX_PATH - 1);

        MessageHeader header = {is_dir ? CMD_MOVE_DIR : CMD_MOVE_FILE, sizeof(MoveRequest), state->session_id};
        if (send_message(state->sockfd, &header, &req) >= 0) {
            void *resp_data = NULL;
            recv_message(state->sockfd, &header, &resp_data);
            if (resp_data) free(resp_data);
            refresh_file_list(state);
        }
    }
    gtk_widget_destroy(dialog);
}

// --- Wrappers cho Context Menu ---
void on_menu_delete(GtkWidget *menuitem, gpointer data) {
    (void)data;
    AppState *state = (AppState *)g_object_get_data(G_OBJECT(menuitem), "state");
    char *filename = (char *)g_object_get_data(G_OBJECT(menuitem), "filename");
    int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem), "is_dir"));
    action_delete_file(state, filename, is_dir);
}

void on_menu_rename(GtkWidget *menuitem, gpointer data) {
    (void)data;
    AppState *state = (AppState *)g_object_get_data(G_OBJECT(menuitem), "state");
    char *filename = (char *)g_object_get_data(G_OBJECT(menuitem), "filename");
    int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem), "is_dir"));
    action_rename_file(state, filename, is_dir);
}

void on_menu_move(GtkWidget *menuitem, gpointer data) {
    (void)data;
    AppState *state = (AppState *)g_object_get_data(G_OBJECT(menuitem), "state");
    char *filename = (char *)g_object_get_data(G_OBJECT(menuitem), "filename");
    int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem), "is_dir"));
    action_move_file(state, filename, is_dir);
}

gboolean on_file_list_right_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right click
        GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(widget), event->y);
        if (row) {
            AppState *state = (AppState *)data;
            char *filename = (char *)g_object_get_data(G_OBJECT(row), "filename");
            int is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
            
            gtk_list_box_select_row(GTK_LIST_BOX(widget), row);
            
        GtkWidget *menu = gtk_menu_new();
        
        GtkWidget *item_del = gtk_menu_item_new_with_label("Delete");
        GtkWidget *item_ren = gtk_menu_item_new_with_label("Rename");
        GtkWidget *item_mov = gtk_menu_item_new_with_label("Move");

        // Attach data to menu items
        g_object_set_data(G_OBJECT(item_del), "state", state); g_object_set_data(G_OBJECT(item_del), "filename", filename); g_object_set_data(G_OBJECT(item_del), "is_dir", GINT_TO_POINTER(is_dir));
        g_object_set_data(G_OBJECT(item_ren), "state", state); g_object_set_data(G_OBJECT(item_ren), "filename", filename); g_object_set_data(G_OBJECT(item_ren), "is_dir", GINT_TO_POINTER(is_dir));
        g_object_set_data(G_OBJECT(item_mov), "state", state); g_object_set_data(G_OBJECT(item_mov), "filename", filename); g_object_set_data(G_OBJECT(item_mov), "is_dir", GINT_TO_POINTER(is_dir));

        g_signal_connect(item_del, "activate", G_CALLBACK(on_menu_delete), NULL);
        g_signal_connect(item_ren, "activate", G_CALLBACK(on_menu_rename), NULL);
        g_signal_connect(item_mov, "activate", G_CALLBACK(on_menu_move), NULL);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_del);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_ren);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_mov);
        
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
        }
    }
    return FALSE;
}

GtkWidget* create_group_page(AppState *state) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    // --- Top Bar: Nút Back và Đường dẫn ---
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *btn_back = gtk_button_new_with_label("⬅ Back");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_file_back_clicked), state);
    
    state->lbl_current_path = gtk_label_new(""); // Nội dung sẽ được refresh_file_list cập nhật
    gtk_label_set_use_markup(GTK_LABEL(state->lbl_current_path), TRUE); // Cho phép dùng thẻ in đậm <b>
    
    gtk_box_pack_start(GTK_BOX(top), btn_back, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top), state->lbl_current_path, TRUE, TRUE, 5);

    // --- File List Area ---
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    state->file_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->file_list_box), GTK_SELECTION_SINGLE);
    // Gắn sự kiện click vào file/folder
    g_signal_connect(state->file_list_box, "row-activated", G_CALLBACK(on_file_row_activated), state);
    g_signal_connect(state->file_list_box, "button-press-event", G_CALLBACK(on_file_list_right_click), state);
    gtk_container_add(GTK_CONTAINER(scroll), state->file_list_box);

    // --- Bottom Bar: Các nút chức năng ---
    GtkWidget *bot = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bot), GTK_BUTTONBOX_CENTER);
    
    GtkWidget *btn_up = gtk_button_new_with_label("Upload File");
    // !!! QUAN TRỌNG: Gắn hàm on_upload_clicked vào nút này !!!
    g_signal_connect(btn_up, "clicked", G_CALLBACK(on_upload_clicked), state);

    GtkWidget *btn_dl = gtk_button_new_with_label("Download Selected");
    GtkWidget *btn_mkdir = gtk_button_new_with_label("New Folder");
    GtkWidget *btn_mems = gtk_button_new_with_label("Members");

    g_signal_connect(btn_dl, "clicked", G_CALLBACK(on_download_clicked), state);
    g_signal_connect(btn_mkdir, "clicked", G_CALLBACK(on_mkdir_clicked), state);
    g_signal_connect(btn_mems, "clicked", G_CALLBACK(on_group_members_clicked), state);

    gtk_container_add(GTK_CONTAINER(bot), btn_up);
    gtk_container_add(GTK_CONTAINER(bot), btn_dl);
    gtk_container_add(GTK_CONTAINER(bot), btn_mkdir);
    gtk_container_add(GTK_CONTAINER(bot), btn_mems);

    // --- Bottom Bar 2: Group Actions ---
    GtkWidget *bot2 = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bot2), GTK_BUTTONBOX_CENTER);

    GtkWidget *btn_invite = gtk_button_new_with_label("Invite User");
    GtkWidget *btn_leave = gtk_button_new_with_label("Leave Group");
    GtkWidget *btn_transfer = gtk_button_new_with_label("Transfer Owner");

    g_signal_connect(btn_invite, "clicked", G_CALLBACK(on_invite_user_clicked), state);
    g_signal_connect(btn_leave, "clicked", G_CALLBACK(on_leave_group_clicked), state);
    g_signal_connect(btn_transfer, "clicked", G_CALLBACK(on_transfer_owner_clicked), state);

    gtk_container_add(GTK_CONTAINER(bot2), btn_invite);
    gtk_container_add(GTK_CONTAINER(bot2), btn_transfer);
    gtk_container_add(GTK_CONTAINER(bot2), btn_leave);

    // --- Đóng gói tất cả vào VBox ---
    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bot, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), bot2, FALSE, FALSE, 5);

    return vbox;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    app_state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app_state.window), "File Sharing Client");
    gtk_window_set_default_size(GTK_WINDOW(app_state.window), 500, 400);
    g_signal_connect(app_state.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    app_state.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app_state.stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    GtkWidget *start_page = create_start_page(&app_state);
    gtk_stack_add_named(GTK_STACK(app_state.stack), start_page, "start_page");

    GtkWidget *login_form = create_login_form(&app_state);
    gtk_stack_add_named(GTK_STACK(app_state.stack), login_form, "login_form");

    GtkWidget *register_form = create_register_form(&app_state);
    gtk_stack_add_named(GTK_STACK(app_state.stack), register_form, "register_form");

    GtkWidget *main_page = create_main_page(&app_state);
    gtk_stack_add_named(GTK_STACK(app_state.stack), main_page, "main_page");

    gtk_stack_add_named(GTK_STACK(app_state.stack), create_group_page(&app_state), "group_page");

    gtk_container_add(GTK_CONTAINER(app_state.window), app_state.stack);

    gtk_widget_show_all(app_state.window);
    gtk_main();

    return 0;
}