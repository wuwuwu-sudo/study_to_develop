let currentMerchantId = null; 
let cart = {};
let allDishes = [];
let currentUser = null;

function showError(msg) {
    const el = document.getElementById('errorModalText');
    const modal = document.getElementById('errorModal');
    if (el && modal) { el.textContent = msg; modal.style.display = 'flex'; }
}
function closeErrorModal() {
    document.getElementById('errorModal').style.display = 'none';
}

// Sidebar navigation
document.querySelectorAll('.dashboard-sidebar a').forEach(a => {
    a.addEventListener('click', function() {
        document.querySelectorAll('.dashboard-sidebar a').forEach(x => x.classList.remove('active'));
        document.querySelectorAll('.panel').forEach(x => x.classList.remove('active'));
        this.classList.add('active');
        document.getElementById('panel-' + this.dataset.panel).classList.add('active');
    });
});

async function apiFetch(url, opts = {}) {
    const resp = await fetch(url, { credentials: 'include', ...opts });
    if (resp.status === 401) {
        window.location.href = '/login.html';
        return null;
    }
    return resp;
}

async function checkLogin() {
    const resp = await apiFetch('/api/user/info');
    if (!resp) return;
    if (!resp.ok) {
        window.location.href = '/login.html';
        return;
    }
    const data = await resp.json();
    if (!data.user_id) {
        window.location.href = '/login.html';
        return;
    }
    currentUser = data;
    document.getElementById('usernameDisplay').textContent = data.username;
}

async function loadShops() {
    const resp = await apiFetch('/api/shops');
    if (!resp) return;
    const shops = await resp.json();
    const container = document.getElementById('shopList');
    container.innerHTML = shops.map(s => `
        <div class="shop-card${currentMerchantId === s.id ? ' selected' : ''}" data-id="${s.id}" data-name="${s.shop_name}">
            <h4>${s.shop_name}</h4>
            <p>${s.address || ''}</p>
        </div>
    `).join('');

    container.querySelectorAll('.shop-card').forEach(card => {
        card.addEventListener('click', function() {
            document.querySelectorAll('.shop-card').forEach(x => x.classList.remove('selected'));
            this.classList.add('selected');
            currentMerchantId = parseInt(this.dataset.id);
            document.getElementById('selectedShopName').textContent = this.dataset.name + ' - 菜单';
            document.getElementById('selectedShopArea').style.display = 'block';
            loadDishes(currentMerchantId);
        });
    });
}

async function loadDishes(merchantId) {
    // ✅ 防止 merchantId 为 null / undefined / NaN
    if (!merchantId || isNaN(merchantId)) {
        console.warn('[loadDishes] 无效的 merchantId:', merchantId);
        return;
    }

    console.log('[loadDishes] 请求菜品, merchantId:', merchantId);

    const resp = await apiFetch('/api/dishes?merchant_id=' + merchantId);
    if (!resp) {
        console.error('[loadDishes] 响应为空');
        return;
    }

    // 检查响应状态
    if (resp.status !== 200) {
        console.error('[loadDishes] HTTP 错误:', resp.status);
        const text = await resp.text();
        console.error('[loadDishes] 错误响应:', text);
        return;
    }

    try {
        const data = await resp.json();
        console.log('[loadDishes] 收到数据:', data);
        
        // ✅ 确保 allDishes 是数组
        if (Array.isArray(data)) {
            allDishes = data;
        } else if (data.dishes && Array.isArray(data.dishes)) {
            allDishes = data.dishes;
        } else {
            console.error('[loadDishes] 数据格式错误:', data);
            allDishes = [];
        }
        
        renderDishes();
    } catch (e) {
        console.error('[loadDishes] JSON 解析失败:', e);
        allDishes = [];
        renderDishes();
    }
}

function renderDishes(filter = 'all') {
    const container = document.getElementById('dishList');
    
    // ✅ 确保 allDishes 是数组
    if (!Array.isArray(allDishes)) {
        console.error('[renderDishes] allDishes 不是数组:', allDishes);
        container.innerHTML = '<p style="color:#888;text-align:center;padding:2rem;">数据加载失败</p>';
        return;
    }

    const filtered = filter === 'all' ? allDishes : allDishes.filter(d => d.category === filter);
    
    if (!filtered.length) {
        container.innerHTML = '<p style="color:#888;text-align:center;padding:2rem;">暂无菜品</p>';
        return;
    }

    container.innerHTML = filtered.map(d => `
        <div class="dish-card" data-id="${d.id}">
            <h4>${d.name || '未命名'}</h4>
            <div class="price">¥${(d.price || 0).toFixed(2)}</div>
            <div class="category">${d.category || '未分类'}</div>
            <div class="desc">${d.description || ''}</div>
            <div class="dish-actions">
                <input type="number" class="qty-input" value="1" min="1" max="99">
                <button class="btn btn-primary btn-small add-to-cart">加入购物车</button>
            </div>
        </div>
    `).join('');

    container.querySelectorAll('.add-to-cart').forEach(btn => {
        btn.addEventListener('click', function() {
            const card = this.closest('.dish-card');
            const dishId = parseInt(card.dataset.id);
            const qty = parseInt(card.querySelector('.qty-input').value) || 1;
            addToCart(dishId, qty);
        });
    });

    // Build category filters
    const categories = [...new Set(allDishes.map(d => d.category).filter(c => c))];
    const filterContainer = document.getElementById('categoryFilters');
    filterContainer.innerHTML = '<button class="active" data-filter="all">全部</button>' +
        categories.map(c => `<button data-filter="${c}">${c}</button>`).join('');

    filterContainer.querySelectorAll('button').forEach(btn => {
        btn.addEventListener('click', function() {
            filterContainer.querySelectorAll('button').forEach(x => x.classList.remove('active'));
            this.classList.add('active');
            renderDishes(this.dataset.filter);
        });
    });
}

function addToCart(dishId, qty) {
    const dish = allDishes.find(d => d.id === dishId);
    if (!dish) return;
    if (!cart[dishId]) cart[dishId] = { ...dish, qty: 0 };
    cart[dishId].qty += qty;
    renderCart();
    document.getElementById('cartArea').style.display = 'block';
}

function renderCart() {
    const container = document.getElementById('cartItems');
    let total = 0;
    const entries = Object.entries(cart).filter(([_, item]) => item.qty > 0);
    container.innerHTML = entries.map(([id, item]) => {
        total += item.price * item.qty;
        return `<div class="cart-item">
            <span>${item.name} x ${item.qty}</span>
            <span>¥${(item.price * item.qty).toFixed(2)}
                <button class="btn btn-danger btn-small" style="margin-left:8px;" onclick="removeFromCart(${id})">x</button>
            </span>
        </div>`;
    }).join('');
    document.getElementById('cartTotal').textContent = '合计: ¥' + total.toFixed(2);
    if (entries.length === 0) document.getElementById('cartArea').style.display = 'none';
}

function removeFromCart(dishId) {
    delete cart[dishId];
    renderCart();
}

document.getElementById('clearCartBtn').addEventListener('click', function() {
    cart = {};
    renderCart();
});

document.getElementById('submitOrderBtn').addEventListener('click', async function() {
    const address = document.getElementById('orderAddress').value.trim();
    if (!address) { showError('请输入送餐地址'); return; }
    const note = document.getElementById('orderNote').value.trim();
    const items = Object.entries(cart).filter(([_, item]) => item.qty > 0).map(([id, item]) => ({
        dish_id: parseInt(id), qty: item.qty
    }));
    if (items.length === 0) { showError('购物车为空'); return; }

    const resp = await apiFetch('/api/order/submit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ merchant_id: currentMerchantId, address, note, items })
    });
    if (!resp) return;
    const data = await resp.json();
    if (data.success) {
        cart = {};
        renderCart();
        document.getElementById('orderAddress').value = '';
        document.getElementById('orderNote').value = '';
        showError('下单成功！订单号: ' + data.order_id);
        loadOrders();
    } else {
        showError(data.message || '下单失败');
    }
});

async function loadOrders() {
    const resp = await apiFetch('/api/orders/my');
    if (!resp) return;
    const orders = await resp.json();
    const container = document.getElementById('orderList');
    if (!orders.length) {
        container.innerHTML = '<p style="color:#888;text-align:center;padding:2rem;">暂无订单</p>';
        return;
    }

    const statusMap = { '0': '待接单', '1': '已接单', '2': '已完成', '-1': '已取消' };
    const statusClass = { '0': 'status-pending', '1': 'status-accepted', '2': 'status-completed', '-1': 'status-pending' };

    container.innerHTML = '<table class="order-table"><thead><tr><th>订单号</th><th>金额</th><th>状态</th><th>地址</th><th>时间</th><th>明细</th></tr></thead><tbody>' +
        orders.map(o => `<tr>
            <td>#${o.id}</td>
            <td>¥${o.total.toFixed(2)}</td>
            <td><span class="status-badge ${statusClass[o.status]}">${statusMap[o.status] || '未知'}</span></td>
            <td>${o.address}</td>
            <td>${o.note || '-'}</td>
            <td>${o.items.map(i => `${i.dish_name}x${i.qty}`).join(', ')}</td>
        </tr>`).join('') + '</tbody></table>';
}

// Logout
document.getElementById('logoutBtn').addEventListener('click', async function() {
    await apiFetch('/api/user/logout', { method: 'POST' });
    window.location.href = '/';
});

// Init
(async function() {
    await checkLogin();
    await loadShops();
    await loadOrders();
    // Poll orders every 10s
    setInterval(loadOrders, 10000);
})();
