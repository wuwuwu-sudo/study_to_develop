#!/bin/bash

cd ~/make_web

echo "=========================================="
echo "  部署 Nginx + WebServer 集成"
echo "=========================================="

# 1. 创建 Nginx 目录
echo "1. 创建 Nginx 目录..."
sudo mkdir -p /var/www/food_delivery
sudo mkdir -p /var/log/nginx/food_delivery
sudo mkdir -p /var/cache/nginx/food_delivery

# 2. 复制静态文件
echo "2. 复制静态文件到 Nginx..."
sudo cp -r ~/make_web/www/* /var/www/food_delivery/
sudo chown -R www-data:www-data /var/www/food_delivery
sudo chmod -R 755 /var/www/food_delivery

# 3. 复制 Nginx 配置文件（从项目目录到系统目录）
echo "3. 复制 Nginx 配置..."
sudo cp ~/make_web/nginx/food_delivery.conf /etc/nginx/sites-available/

# 4. 启用配置
echo "4. 启用 Nginx 配置..."
sudo ln -sf /etc/nginx/sites-available/food_delivery \
           /etc/nginx/sites-enabled/food_delivery

# 5. 测试 Nginx 配置
echo "5. 测试 Nginx 配置..."
if sudo nginx -t; then
    echo "  ✅ Nginx 配置正确"
else
    echo "  ❌ Nginx 配置错误"
    exit 1
fi

# 6. 重新加载 Nginx
echo "6. 重新加载 Nginx..."
sudo systemctl reload nginx

# 7. 设置权限
echo "7. 设置权限..."
sudo chown -R www-data:www-data /var/www/food_delivery
sudo chown -R www-data:www-data /var/log/nginx/food_delivery
sudo chown -R www-data:www-data /var/cache/nginx/food_delivery

echo ""
echo "=========================================="
echo "✅ Nginx 部署完成！"
echo ""
echo "配置来源: ~/make_web/nginx/food_delivery.conf"
echo "部署位置: /etc/nginx/sites-available/food_delivery"
echo ""
echo "⚠️  反向代理指向三实例 8081/8082/8083，"
echo "    请先启动后端实例（见 README「三实例部署」）："
echo "      cd ~/make_web && ./start_servers.sh"
echo ""
echo "访问地址:"
echo "  首页: http://localhost/"
echo "  静态文件: http://localhost/styles.css"
echo "  API: http://localhost/api/shops"
echo "  健康检查: http://localhost/health"
echo "=========================================="
