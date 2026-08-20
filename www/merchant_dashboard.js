let currentDishes = [];
let currentOrders = [];
let merchantInfo = null;

function showError(msg) {
    const el = document.getElementById('errorModalText');
    const modal = document.getElementById('errorModal');
    if (el && modal) { el.textContent = msg; modal.style.display = 'flex'; }
}
function closeErrorModal() {
    document.getElementById('errorModal').style.display = 'none';
}
function closeDishModal() {
    document.getElementById('dishModal').style.display = 'none';
}

// Sidebar nav
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
    if (resp.status === 401) { window.location.href = '/merchant_login.html'; return null; }
    return resp;
}

async function checkLogin() {
    const resp = await apiFetch('/api/merchant/info');
    if (!resp) return;

    if (!resp.ok) {
        window.location.href = '/merchant_login.html';
        return;
    }

    const data = await resp.json();
    merchantInfo = data;
    document.getElementById('shopNameDisplay').textContent = data.shop_name;

    const toggle = document.getElementById('statusToggle');
    toggle.checked = data.status === 1;
    document.getElementById('statusLabel').textContent =
        data.status === 1 ? '营业中' : '休息中';
}

// Status toggle
document.getElementById('statusToggle').addEventListener('change', async function() {
    const status = this.checked ? 1 : 0;
    document.getElementById('statusLabel').textContent = status === 1 ? '营业中' : '休息中';
    await apiFetch('/api/merchant/status', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ status })
    });
});

// Load dishes
async function loadDishes() {
    const resp = await apiFetch('/api/dishes');
    if (!resp) return;
    const result = await resp.json();
    currentDishes = result.dishes;
    renderDishTable();
}

function renderDishTable() {
    const container = document.getElementById('dishTableContainer');
    const dishes = Array.isArray(currentDishes)
                    ? currentDishes
                    : (currentDishes?.dishes || []);
    if (!dishes.length) {
        container.innerHTML = '<p style="color:#888;text-align:center;padding:2rem;">暂无菜品，点击右上方新增</p>';
        return;
    }
    container.innerHTML = `<table class="dish-table">
        <thead><tr><th>名称</th><th>分类</th><th>价格</th><th>状态</th><th>操作</th></tr></thead>
        <tbody>
        ${dishes.map(d => `
            <tr>
                <td>${d.name}</td>
                <td>${d.category || '-'}</td>
                <td>¥${d.price.toFixed(2)}</td>
                <td>${d.available ? '<span style="color:#27ae60;">在售</span>' : '<span style="color:#888;">下架</span>'}</td>
                <td class="actions">
                    <button class="btn btn-small btn-outline" onclick="editDish(${d.id})">编辑</button>
                    <button class="btn btn-small ${d.available ? 'btn-secondary' : 'btn-success'}" onclick="toggleAvailable(${d.id}, ${!d.available})">
                        ${d.available ? '下架' : '上架'}
                    </button>
                    <button class="btn btn-small btn-danger" onclick="deleteDish(${d.id})">删除</button>
                </td>
            </tr>
        `).join('')}
        </tbody></table>`;
}

// Add dish button
document.getElementById('addDishBtn').addEventListener('click', function() {
    document.getElementById('dishModalTitle').textContent = '新增菜品';
    document.getElementById('editDishId').value = '';
    document.getElementById('dishName').value = '';
    document.getElementById('dishPrice').value = '';
    document.getElementById('dishCategory').value = '';
    document.getElementById('dishDesc').value = '';
    document.getElementById('dishModalError').classList.add('hidden');
    document.getElementById('dishModal').style.display = 'flex';
    document.getElementById('saveDishBtn').onclick = saveNewDish;
});

// 添加菜品
async function saveNewDish() {
    const name = document.getElementById('dishName').value.trim();
    const priceYuan = parseFloat(document.getElementById('dishPrice').value);
    if (!name || isNaN(priceYuan) || priceYuan < 0) {
        document.getElementById('dishModalError').textContent = '名称和价格不能为空，且价格不能为负数';
        document.getElementById('dishModalError').classList.remove('hidden');
        return;
    }
    const category = document.getElementById('dishCategory').value.trim();
    const desc = document.getElementById('dishDesc').value.trim();

    const resp = await apiFetch('/api/dish/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ name, price: priceYuan, category, description: desc })
    });
    if (!resp) return;
    const data = await resp.json();
    if (data.success) {
        closeDishModal();
        loadDishes();
    } else {
        showError(data.message || '添加失败');
    }
}

// 编辑菜品（回填）
function editDish(dishId) {
    const dishes = Array.isArray(currentDishes)
        ? currentDishes
        : (currentDishes?.dishes || []);
    const dish = dishes.find(d => d.id === dishId);
    if (!dish) return;
    document.getElementById('dishModalTitle').textContent = '编辑菜品';
    document.getElementById('editDishId').value = dishId;
    document.getElementById('dishName').value = dish.name;
    document.getElementById('dishPrice').value = dish.price;           // 直接使用元
    document.getElementById('dishCategory').value = dish.category || '';
    document.getElementById('dishDesc').value = dish.description || '';
    document.getElementById('dishModalError').classList.add('hidden');
    document.getElementById('dishModal').style.display = 'flex';
    document.getElementById('saveDishBtn').onclick = saveEditDish;
}


// 保存编辑（PUT 方法）
async function saveEditDish() {
    const id = parseInt(document.getElementById('editDishId').value);
    const name = document.getElementById('dishName').value.trim();
    const priceYuan = parseFloat(document.getElementById('dishPrice').value);
    if (!name || isNaN(priceYuan) || priceYuan < 0) {
        document.getElementById('dishModalError').textContent = '名称和价格不能为空，且价格不能为负数';
        document.getElementById('dishModalError').classList.remove('hidden');
        return;
    }
    const category = document.getElementById('dishCategory').value.trim();
    const desc = document.getElementById('dishDesc').value.trim();

    const resp = await apiFetch('/api/dish/edit', {
        method: 'PUT',   // 改为 PUT 匹配后端
        headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ id, name, price: priceYuan, category, description: desc })
    });
    if (!resp) return;
    const data = await resp.json();
    if (data.success) {
        closeDishModal();
        loadDishes();    // 刷新列表
    } else {
        showError(data.message || '编辑失败');
    }
}

async function toggleAvailable(dishId, available) {
    await apiFetch('/api/dish/available', {
        method: 'PUT', headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ id: dishId, available })
    });
    loadDishes();
}

async function deleteDish(dishId) {
    if (!confirm('确定要删除该菜品吗？')) return;
    await apiFetch('/api/dish/delete', {
        method: 'PUT', headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ id: dishId })
    });
    loadDishes();
}

// Load orders
async function loadOrders() {
    const resp = await apiFetch('/api/merchant/orders');
    if (!resp) return;
    currentOrders = await resp.json();
    renderOrderTable();
}

function renderOrderTable() {
    const container = document.getElementById('orderTableContainer');
    if (!currentOrders.length) {
        container.innerHTML = '<p style="color:#888;text-align:center;padding:2rem;">暂无订单</p>';
        return;
    }
    const statusMap = { '0': '待接单', '1': '已接单', '2': '已完成', '-1': '已取消' };
    const statusClass = { '0': 'status-pending', '1': 'status-accepted', '2': 'status-completed', '-1': 'status-pending' };

    container.innerHTML = '<table class="order-table"><thead><tr><th>订单号</th><th>顾客</th><th>金额</th><th>地址</th><th>备注</th><th>明细</th><th>状态</th><th>操作</th></tr></thead><tbody>' +
        currentOrders.map(o => `<tr>
            <td>#${o.id}</td>
            <td>${o.customer_name || '顾客' + o.customer_id}</td>
            <td>¥${o.total.toFixed(2)}</td>
            <td>${o.address}</td>
            <td>${o.note || '-'}</td>
            <td>${o.items.map(i => `${i.dish_name}x${i.qty}`).join(', ')}</td>
            <td><span class="status-badge ${statusClass[o.status]}">${statusMap[o.status] || '未知'}</span></td>
            <td class="actions">
                ${o.status === 0 ? `<button class="btn btn-small btn-success" onclick="updateOrderStatus(${o.id}, 1)">接单</button>` : ''}
                ${o.status === 1 ? `<button class="btn btn-small btn-primary" onclick="updateOrderStatus(${o.id}, 2)">完成</button>` : ''}
                ${o.status === 0 ? `<button class="btn btn-small btn-danger" onclick="updateOrderStatus(${o.id}, -1)">取消</button>` : ''}
            </td>
        </tr>`).join('') + '</tbody></table>';
}

async function updateOrderStatus(orderId, status) {
    await apiFetch('/api/order/status', {
        method: 'PUT', headers: { 'Content-Type': 'application/json; charset=utf-8' },
        body: JSON.stringify({ order_id: orderId, status })
    });
    loadOrders();
}

// Logout
document.getElementById('logoutBtn').addEventListener('click', async function() {
    await apiFetch('/api/merchant/logout', { method: 'POST' });
    window.location.href = '/';
});

// Init
(async function() {
    await checkLogin();
    await loadDishes();
    await loadOrders();
    setInterval(loadOrders, 10000);
})();
