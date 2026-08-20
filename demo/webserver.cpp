#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <cctype>
#include <string>
#include "handle_user_and_merchant.h"
#include "handle_dishes.h"
#include "init_db.h"
#include "log.h"

constexpr int MAX_EVENTS = 1024;
constexpr int BUF_SIZE = 128;


int main(int argc, char* argv[]) {
    init_database(); 

    // ========== 解析端口参数 ==========
    int port = 8080;  // 默认端口
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::stoi(argv[i + 1]);
            i++;
        }
    }

    // 根据端口设置不同的日志文件
    std::string log_file = "server_" + std::to_string(port) + ".log";
    Logger::instance().set_file(log_file);

    // 1. 创建监听 socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // 2. 绑定
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    // 3. 监听
    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    std::cout << "HTTP server running on http://localhost:" << port << std::endl; 

    // 4. 创建 epoll
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return 1;
    }

    // 5. 把监听 socket 加入 epoll
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    epoll_event events[MAX_EVENTS];
    LOG_INFO("WebServer starting...");

    // 6. 事件循环
    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            // ===== 新连接 =====
            if (fd == listen_fd) {
                sockaddr_in client{};
                socklen_t len = sizeof(client);
                int client_fd = accept(listen_fd, (sockaddr*)&client, &len);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                char ip[64];
                std::cout << "new client: "
                          << inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip))
                          << ":" << ntohs(client.sin_port) << std::endl;

                // 把客户端 fd 加入 epoll
                epoll_event cli_ev{};
                cli_ev.events = EPOLLIN;
                cli_ev.data.fd = client_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cli_ev);
            }
            // ===== 客户端数据 =====
            else {
                // 1. 读取请求（先读一点，不然可能阻塞）
                char buf[4096] = {0};
                int nread = read(fd, buf, sizeof(buf) - 1);

                std::cout << "--- Request ---\n" << buf << std::endl;
                if (nread <= 0) {
                    // 客户端断开，清理资源
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    continue;
                }

                // ---- 解析请求行：GET /about HTTP/1.1 ----
                char method[16] = {0};
                char path[256] = {0};
                char version[32] = {0};

                sscanf(buf, "%s %s %s", method, path, version);

                std::string original_path = path;

                // ========== ✅ 去掉查询参数 ==========
                char* query_pos = strchr(path, '?');
                if (query_pos != nullptr) {
                    *query_pos = '\0';  // 截断，只保留路径部分
                }

                std::cout << "Parsed path: " << path << std::endl;

                std::string request(buf);
            /* ================= 页面路由 =================(路由分发：根据路径返回不同文件)*/
                /* ---- 首页 ---- */
                if (strcmp(path, "/") == 0) {
                    send_file(fd, "www/index.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 顾客登录 ---- */
                else if (strcmp(path, "/login.html") == 0) {
                    send_file(fd, "www/login.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 顾客注册 ---- */
                else if (strcmp(path, "/register.html") == 0) {
                    send_file(fd, "www/register.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 商家登录 ---- */
                else if (strcmp(path, "/merchant_login.html") == 0) {
                    send_file(fd, "www/merchant_login.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 商家注册 ---- */
                else if (strcmp(path, "/merchant_register.html") == 0) {
                    send_file(fd, "www/merchant_register.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 顾客后台 ---- */
                else if (strcmp(path, "/customer_dashboard.html") == 0) {
                    int user_id = get_user_id_by_session(request);
                    if (user_id <= 0) {
                        std::string header =
                            "HTTP/1.1 302 Found\r\n"
                            "Location: /login.html\r\n"
                            "Content-Length: 0\r\n\r\n";
                        write(fd, header.data(), header.size());
                        close(fd);
                        continue;
                    }
                    send_file(fd, "www/customer_dashboard.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ---- 商家后台 ---- */
                else if (strcmp(path, "/merchant_dashboard.html") == 0) {
                    int merchant_id = get_merchant_id_by_session(request);
                    if (merchant_id <= 0) {
                        std::string header =
                            "HTTP/1.1 302 Found\r\n"
                            "Location: /merchant_login.html\r\n"
                            "Content-Length: 0\r\n\r\n";
                        write(fd, header.data(), header.size());
                        close(fd);
                        continue;
                    }
                    send_file(fd, "www/merchant_dashboard.html", "text/html; charset=utf-8");
                    continue;
                }
                /* ================= API 路由 ================= */
                /* ---- 顾客登录 ---- */
                else if (strcmp(path, "/api/user/login") == 0 && strcmp(method, "POST") == 0) {
                    handle_user_login(fd, request);
                    continue;
                }
                /* ---- 顾客注册 ---- */
                else if (strcmp(path, "/api/user/register") == 0 && strcmp(method, "POST") == 0) {
                    handle_user_register(fd, request);
                    continue;
                }
                /* ---- 顾客信息 ---- */
                else if (strcmp(path, "/api/user/info") == 0 && strcmp(method, "GET") == 0) {
                    handle_user_info(fd, request);
                    continue;
                }
                /* ---- 顾客订单列表 ---- */
                /* ---- 顾客订单列表 ---- */
                else if (strcmp(path, "/api/orders/my") == 0 && strcmp(method, "GET") == 0) {
                    handle_my_orders(fd, request);
                    continue;
                }
                /* ---- 获取商家列表 ---- */
                else if (strcmp(path, "/api/shops") == 0 && strcmp(method, "GET") == 0) {
                    handle_get_shops(fd, request);
                    continue;
                }
                /*---- 顾客登出 ----*/
                else if (strcmp(path, "/api/user/logout") == 0 && strcmp(method, "POST") == 0) {
                    handle_user_logout(fd, request);
                    continue;
                }
                /* ---- 商家登录 ---- */
                else if (strcmp(path, "/api/merchant/login") == 0 && strcmp(method, "POST") == 0) {
                    handle_merchant_login(fd, request);
                    continue;
                }
                /* ---- 商家注册 ---- */
                else if (strcmp(path, "/api/merchant/register") == 0 && strcmp(method, "POST") == 0) {
                    handle_merchant_register(fd, request);
                    continue;
                }
                /* ---- 商家信息 ---- */
                else if (strcmp(path, "/api/merchant/info") == 0 && strcmp(method, "GET") == 0) {
                    handle_merchant_info(fd, request);
                    continue;
                }
                /* ---- 商家登出 ---- */
                else if (strcmp(path, "/api/merchant/logout") ==0 && strcmp(method, "POST") == 0) {
                    handle_merchant_logout(fd, request);
                    continue;
                }
                /* ---- 菜品列表 ---- */
                else if (strcmp(path, "/api/dishes") == 0 && strcmp(method, "GET") == 0) {
                    handle_get_dishes(fd, request);
                    continue;
                }
                /* ---- 新增菜品 ---- */
                else if (strcmp(path, "/api/dish/add") == 0 && strcmp(method, "POST") == 0) {
                    handle_add_dish(fd, request);
                    continue;
                }
                /* ---- 修改菜品 ---- */
                else if (strcmp(path, "/api/dish/edit") == 0 && strcmp(method, "PUT") == 0) {
                    handle_edit_dish(fd, request);
                    continue;
                }
                /* ---- 菜品上下架 ---- */
                else if (strcmp(path, "/api/dish/available") == 0 && strcmp(method, "PUT") == 0) {
                    handle_dish_available(fd, request);
                    continue;
                }
                /* ---- 删除菜品 ---- */
                else if (strcmp(path, "/api/dish/delete") == 0 && strcmp(method, "PUT") == 0) {
                    handle_delete_dish(fd, request);
                    continue;
                }
                /* ---- 商家状态更新 ---- */
                else if (strcmp(path, "/api/merchant/status") == 0 && strcmp(method, "PUT") == 0) {
                    handle_merchant_status(fd, request);
                    continue;
                }
                /* ---- 更新订单状态 ---- */
                else if (strcmp(path, "/api/order/status") == 0 && strcmp(method, "PUT") == 0) {
                    handle_update_order_status(fd, request);
                    continue;
                }
                /* ---- 商家订单 ---- */
                else if (strcmp(path, "/api/merchant/orders") == 0 && strcmp(method, "GET") == 0) {
                    handle_merchant_orders(fd, request);
                    continue;
                }
                /* ---- 提交订单 ---- */
                else if (strcmp(path, "/api/order/submit") == 0 && strcmp(method, "POST") == 0) {
                    handle_order_submit(fd, request);
                    continue;
                }
                /* ================= 静态资源 ================= */
                /* ---- JS ---- */
                else if (ends_with(path, ".js")) {
                    send_file(fd, ("www" + std::string(path)).c_str(),
                            "application/javascript; charset=utf-8");
                    continue;
                }
                /* ---- CSS ---- */
                else if (ends_with(path, ".css")) {
                    send_file(fd, ("www" + std::string(path)).c_str(),
                            "text/css; charset=utf-8");
                    continue;
                }
                /* ---- 图片 ---- */
                else if (starts_with(path, "/images/")) {
                    send_file(fd, ("www" + std::string(path)).c_str(),
                            get_mime(path));
                    continue;
                }
                /* ---- 图标 ---- */
                else if (ends_with(path, ".ico")) {
                    send_file(fd, ("www" + std::string(path)).c_str(),
                            "image/x-icon");
                    continue;
                }
                /* ================= 404 ================= */
                else {
                    send_404(fd);
                    continue;
                }

                shutdown(fd, SHUT_WR);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
            }
        } 
    }

    close(listen_fd);
    close(epfd);
    return 0;
}