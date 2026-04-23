// TD-light Lightcurve Portal
let lightcurveChart = null;
let skyMap3D = null;
let is3DMode = true;
let currentObjects = [];
let selectedObjects = []; 
let isClassificationRunning = false;
let currentLightcurveData = null;

window.switchTab = function(tabName) {
    document.querySelectorAll('.nav-tab').forEach(el => el.classList.remove('active'));
    document.getElementById('nav-' + tabName)?.classList.add('active');
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.getElementById(tabName + 'Tab')?.classList.add('active');
    if (tabName === 'settings') window.loadConfig();
};

window.loadConfig = async function() {
    try {
        const response = await fetch('/api/config');
        const config = await response.json();
        document.getElementById('configDbName').value = config.database?.name || '';
        document.getElementById('configDbHost').value = config.database?.host || '';
        document.getElementById('configDbPort').value = config.database?.port || '';
        document.getElementById('configDbUser').value = config.database?.user || '';
        document.getElementById('configThreads').value = config.import?.threads || 16;
        document.getElementById('configVgroups').value = config.import?.vgroups || 32;
        document.getElementById('configThreshold').value = config.classification?.confidence_threshold || 0.95;
        document.getElementById('configModelPath').value = config.classification?.model_path || '';
        
        // 加载数据库列表
        try {
            const dbRes = await fetch('/api/databases');
            const dbData = await dbRes.json();
            const select = document.getElementById('dbListSelect');
            if (select && dbData.databases) {
                select.innerHTML = '<option value="">▼</option>' + 
                    dbData.databases.map(db => `<option value="${db}">${db}</option>`).join('');
                
                // 设置下拉框选中当前数据库（如果有）
                if (config.database?.name && dbData.databases.includes(config.database.name)) {
                    select.value = config.database.name;
                }
            }
        } catch (e) {
            console.error('Failed to load databases:', e);
        }

        showToast(tMsg('msg_config_loaded'), 'success');
    } catch (e) {
        showToast(tMsg('msg_config_load_failed') + ': ' + e.message, 'error');
    }
};

window.saveConfig = async function() {
    try {
        const configData = {
            db_name: document.getElementById('configDbName').value,
            db_host: document.getElementById('configDbHost').value,
            db_port: parseInt(document.getElementById('configDbPort').value) || 6041,
            threads: parseInt(document.getElementById('configThreads').value) || 16,
            vgroups: parseInt(document.getElementById('configVgroups').value) || 32,
            confidence_threshold: parseFloat(document.getElementById('configThreshold').value) || 0.95
        };
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configData)
        });
        const result = await response.json();
        if (result.success) {
            showToast(tMsg('msg_config_saved'), 'success');
        } else {
            showToast(tMsg('msg_config_save_failed') + ': ' + (result.error || ''), 'error');
        }
    } catch (e) {
        showToast(tMsg('msg_config_save_failed') + ': ' + e.message, 'error');
    }
};

// 从 config.json 同步配置到后端和前端
window.syncConfigFromFile = async function() {
    try {
        // 1. 让后端重新读取 config.json
        const reloadResp = await fetch('/api/config/reload');
        const reloadResult = await reloadResp.json();
        
        if (!reloadResult.success) {
            showToast('重载配置失败: ' + (reloadResult.error || '未知错误'), 'error');
            return;
        }
        
        // 2. 获取新配置并更新前端
        const configResp = await fetch('/api/config');
        const config = await configResp.json();
        
        // 更新数据导入页面的数据库名
        const importDbInput = document.getElementById('importDbName');
        if (importDbInput && config.database?.name) {
            importDbInput.value = config.database.name;
        }
        
        // 更新 nside
        const nsideInput = document.getElementById('importNside');
        if (nsideInput && config.healpix?.nside) {
            nsideInput.value = config.healpix.nside;
        }
        
        showToast(tMsg('msg_config_synced') + ': ' + (config.database?.name || ''), 'success');
        
        // 刷新待分类数量
        window.onDbNameChange();
        
    } catch (e) {
        showToast(tMsg('msg_config_sync_failed') + ': ' + e.message, 'error');
    }
};

window.reloadConfigBackend = async function() {
    try {
        // 获取前端当前的配置值
        const configData = {
            db_name: document.getElementById('configDbName').value,
            db_host: document.getElementById('configDbHost').value,
            db_port: parseInt(document.getElementById('configDbPort').value) || 6041,
            confidence_threshold: parseFloat(document.getElementById('configThreshold').value) || 0.95
        };
        
        // 1. 先保存配置到文件
        const saveResponse = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configData)
        });
        const saveResult = await saveResponse.json();
        
        if (!saveResult.success) {
            showToast(tMsg('msg_config_save_failed') + ': ' + (saveResult.error || ''), 'error');
            return;
        }
        
        // 2. 重新加载并应用到后端
        const reloadResponse = await fetch('/api/config/reload');
        const reloadResult = await reloadResponse.json();
        
        if (reloadResult.success) {
            showToast(tMsg('msg_config_applied'), 'success');
        } else {
            showToast(tMsg('msg_apply_failed') + ': ' + (reloadResult.error || ''), 'error');
        }
    } catch (e) {
        showToast(tMsg('msg_apply_failed') + ': ' + e.message, 'error');
    }
};

window.performConeSearch = async function(appendMode = false) {
    const ra = parseFloat(document.getElementById('centerRa').value);
    const dec = parseFloat(document.getElementById('centerDec').value);
    const radius = parseFloat(document.getElementById('radius').value);
    
    if (isNaN(ra) || isNaN(dec) || isNaN(radius)) {
        showToast(tMsg('msg_enter_valid_coords'), 'error');
        return;
    }
    
    try {
        showToast(tMsg('msg_searching'), 'success');
        const response = await fetch(`/api/cone_search?ra=${ra}&dec=${dec}&radius=${radius}&limit=200`);
        const data = await response.json();
        
        if (data.objects && data.objects.length > 0) {
            if (appendMode) {
                const existingIds = new Set(selectedObjects.map(o => String(o.source_id)));
                const newObjects = data.objects.filter(obj => !existingIds.has(String(obj.source_id)));
                
                if (newObjects.length > 0) {
                    currentObjects = [...currentObjects, ...newObjects];
                    selectedObjects = [...selectedObjects, ...newObjects.map(obj => ({...obj, selected: true}))];
                    showToast(tMsg('msg_appended_objects', newObjects.length), 'success');
                } else {
                    showToast(tMsg('msg_all_in_list'), 'error');
                }
            } else {
                currentObjects = data.objects;
                selectedObjects = data.objects.map(obj => ({...obj, selected: true}));
                showToast(tMsg('msg_found_objects', data.objects.length), 'success');
            }
            updateObjectList();
            if (appendMode) {
                // 追加模式：添加新区域，保留旧区域
                allSearchRegions.push({type: 'cone', ra, dec, radius});
                plotSkyMap(currentObjects, null, allSearchRegions);
            } else {
                // 新检索：重置区域列表
                allSearchRegions = [{type: 'cone', ra, dec, radius}];
                plotSkyMap(currentObjects, {ra, dec, radius}, allSearchRegions);
            }
        } else {
            showToast(tMsg('msg_no_objects_found'), 'error');
        }
    } catch (error) {
        showToast(tMsg('msg_search_failed') + ': ' + error.message, 'error');
    }
};

window.searchById = async function(appendMode = false) {
    const input = document.getElementById('searchSourceId').value.trim();
    if (!input) {
        showToast(tMsg('msg_enter_source_id'), 'error');
        return;
    }
    
    const ids = input.split('\n')
        .map(line => line.trim())
        .filter(line => line.length > 0);
    
    if (ids.length === 0) {
        showToast(tMsg('msg_enter_valid_id'), 'error');
        return;
    }
    
    try {
        showToast(tMsg('msg_retrieving', ids.length), 'success');
        
        const foundObjects = [];
        const notFound = [];
        
        for (const id of ids) {
            try {
                const response = await fetch(`/api/object_by_id?id=${id}`);
                const data = await response.json();
                if (data.objects && data.objects.length > 0) {
                    foundObjects.push(...data.objects);
                } else {
                    notFound.push(id);
                }
            } catch (e) {
                notFound.push(id);
            }
        }
        
        if (foundObjects.length > 0) {
            if (appendMode) {
                const existingIds = new Set(selectedObjects.map(o => String(o.source_id)));
                const newObjects = foundObjects.filter(obj => !existingIds.has(String(obj.source_id)));
                
                if (newObjects.length > 0) {
                    currentObjects = [...currentObjects, ...newObjects];
                    selectedObjects = [...selectedObjects, ...newObjects.map(obj => ({...obj, selected: true}))];
                    showToast(tMsg('msg_appended_objects', newObjects.length), 'success');
                } else {
                    showToast(tMsg('msg_all_in_list'), 'error');
                }
            } else {
                currentObjects = foundObjects;
                selectedObjects = foundObjects.map(obj => ({...obj, selected: true}));
                showToast(tMsg('msg_found_objects', foundObjects.length), 'success');
            }
            
            updateObjectList();
            plotSkyMap(currentObjects, null);
            
            const obj = foundObjects[0];
            if (obj.table_name) {
                highlightedObjectId = obj.source_id;
                loadLightcurveForObject(obj.table_name);
                updateMetadata(obj, obj.data_count);
                document.getElementById('objectInfo').textContent = 'Source ID: ' + obj.source_id;
            }
        } else {
            showToast(tMsg('msg_not_found'), 'error');
        }
        
        if (notFound.length > 0) {
            console.log('IDs not found:', notFound);
        }
    } catch (error) {
        showToast(tMsg('msg_retrieve_failed') + ': ' + error.message, 'error');
    }
};

window.clearObjectList = function() {
    currentObjects = [];
    selectedObjects = [];
    highlightedObjectId = null;
    allSearchRegions = [];
    currentConeRegion = null;
    updateObjectList();
    
    const placeholder = document.getElementById('skyMapPlaceholder');
    if (placeholder) placeholder.style.display = 'block';
    const skyMap3DEl = document.getElementById('skyMap3D');
    if (skyMap3DEl) skyMap3DEl.innerHTML = '';
    
    showToast(tMsg('msg_list_cleared'), 'success');
};

window.selectAllObjects = function(selectAll) {
    selectedObjects.forEach(obj => obj.selected = selectAll);
    updateObjectList();
};

window.performRectSearch = async function(appendMode = false) {
    const raMin = parseFloat(document.getElementById('raMin').value);
    const raMax = parseFloat(document.getElementById('raMax').value);
    const decMin = parseFloat(document.getElementById('decMin').value);
    const decMax = parseFloat(document.getElementById('decMax').value);
    
    if (isNaN(raMin) || isNaN(raMax) || isNaN(decMin) || isNaN(decMax)) {
        showToast(tMsg('msg_enter_valid_range'), 'error');
        return;
    }
    
    if (raMin >= raMax || decMin >= decMax) {
        showToast(tMsg('msg_min_less_max'), 'error');
        return;
    }
    
    try {
        showToast(tMsg('msg_searching'), 'success');
        const response = await fetch(`/api/region_search?ra_min=${raMin}&ra_max=${raMax}&dec_min=${decMin}&dec_max=${decMax}&limit=200`);
        const data = await response.json();
        
        if (data.objects && data.objects.length > 0) {
            if (appendMode) {
                const existingIds = new Set(selectedObjects.map(o => String(o.source_id)));
                const newObjects = data.objects.filter(obj => !existingIds.has(String(obj.source_id)));
                
                if (newObjects.length > 0) {
                    currentObjects = [...currentObjects, ...newObjects];
                    selectedObjects = [...selectedObjects, ...newObjects.map(obj => ({...obj, selected: true}))];
                    showToast(tMsg('msg_appended_objects', newObjects.length), 'success');
                } else {
                    showToast(tMsg('msg_all_in_list'), 'error');
                }
            } else {
                currentObjects = data.objects;
                selectedObjects = data.objects.map(obj => ({...obj, selected: true}));
                showToast(tMsg('msg_found_objects', data.objects.length), 'success');
            }
            updateObjectList();
            const rectRegion = {type: 'rect', raMin, raMax, decMin, decMax};
            if (appendMode) {
                allSearchRegions.push(rectRegion);
                plotSkyMap(currentObjects, null, allSearchRegions);
            } else {
                allSearchRegions = [rectRegion];
                plotSkyMap(currentObjects, null, allSearchRegions);
            }
        } else {
            showToast(tMsg('msg_no_objects_found'), 'error');
        }
    } catch (error) {
        showToast(tMsg('msg_search_failed') + ': ' + error.message, 'error');
    }
};

function updateObjectList() {
    const listEl = document.getElementById('objectList');
    const countEl = document.getElementById('selectedCount');
    if (!listEl) return;
    
    const selectedCount = selectedObjects.filter(o => o.selected).length;
    countEl.textContent = selectedCount;
    
    if (selectedObjects.length === 0) {
        listEl.innerHTML = `<div class="empty-state">
            <div class="empty-state-icon"><i class="fas fa-search"></i></div>
            <div>使用上方搜索功能查找天体</div>
        </div>`;
        document.getElementById('classifyBtn').disabled = true;
        return;
    }
    
    listEl.innerHTML = selectedObjects.map((obj, idx) => `
        <div class="object-item ${obj.selected ? 'selected' : ''}" onclick="toggleObjectSelection(${idx})">
            <input type="checkbox" class="object-checkbox" ${obj.selected ? 'checked' : ''} onclick="event.stopPropagation(); toggleObjectSelection(${idx})">
            <div style="flex: 1; min-width: 0;">
                <div style="font-size: 0.8rem; font-family: monospace; overflow: hidden; text-overflow: ellipsis;">${obj.source_id}</div>
                <div style="font-size: 0.75rem; color: #64748b;">RA: ${obj.ra?.toFixed(3)}° DEC: ${obj.dec?.toFixed(3)}°</div>
            </div>
            <button class="btn" style="padding: 4px 8px; width: auto; font-size: 0.7rem;" onclick="event.stopPropagation(); viewObject('${obj.source_id}');">
                <i class="fas fa-chart-line"></i>
            </button>
        </div>
    `).join('');
    
    document.getElementById('classifyBtn').disabled = selectedCount === 0;
}

window.toggleObjectSelection = function(idx) {
    selectedObjects[idx].selected = !selectedObjects[idx].selected;
    updateObjectList();
};

window.viewObject = async function(sourceId) {
    let obj = selectedObjects.find(o => String(o.source_id) === String(sourceId));
    if (!obj || !obj.table_name) {
        showToast(tMsg('msg_object_not_found'), 'error');
        return;
    }
    
    try {
        // 尝试获取最新元数据（特别是分类信息）
        try {
            const metaRes = await fetch(`/api/object/${obj.table_name}`);
            const metaJson = await metaRes.json();
            if (metaJson.objects && metaJson.objects.length > 0) {
                const fresh = metaJson.objects[0];
                obj = { ...obj, ...fresh };
                // 更新内存中的对象
                const idx = selectedObjects.findIndex(o => String(o.source_id) === String(sourceId));
                if (idx !== -1) selectedObjects[idx] = obj;
            }
        } catch (e) {
            console.warn('Refresh metadata failed', e);
        }

        const response = await fetch(`/api/lightcurve/${obj.table_name}`);
        const data = await response.json();
        if (data.data && data.data.length > 0) {
            currentLightcurveData = data.data; 
            plotLightcurve(data);
            updateMetadata(obj, data.data.length);
            document.getElementById('chartPlaceholder').style.display = 'none';
            document.getElementById('lightcurveChart').style.display = 'block';
            document.getElementById('objectInfo').textContent = 'Source ID: ' + sourceId;
            
            highlightedObjectId = sourceId;
            if (currentObjects.length > 0) {
                plotSkyMap(currentObjects, currentConeRegion);
            }
        } else {
            showToast(tMsg('msg_no_lc_data'), 'error');
        }
    } catch (error) {
        showToast(tMsg('msg_get_lc_failed') + ': ' + error.message, 'error');
    }
};

function updateMetadata(meta, count) {
    if (!meta) return;
    document.getElementById('metaSourceId').textContent = meta.source_id || '-';
    document.getElementById('metaHealpixId').textContent = meta.healpix_id || '-';
    document.getElementById('metaRa').textContent = meta.ra?.toFixed(4) + '°' || '-';
    document.getElementById('metaDec').textContent = meta.dec?.toFixed(4) + '°' || '-';
    document.getElementById('metaClass').textContent = formatClassName(meta.object_class) || 'UNKNOWN';
    document.getElementById('metaCount').textContent = count || '-';
    document.getElementById('objectInfo').textContent = `Source ID: ${meta.source_id}`;
}

let classifyEventSource = null;

window.classifySelectedObjects = async function() {
    const selected = selectedObjects.filter(o => o.selected);
    if (selected.length === 0) {
        showToast(tMsg('msg_select_objects'), 'error');
        return;
    }
    
    const classifyBtn = document.getElementById('classifyBtn');
    const progressPanel = document.getElementById('progressPanel');
    const progressBar = document.getElementById('progressBar');
    const progressText = document.getElementById('progressText');
    
    classifyBtn.disabled = true;
    classifyBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> 分类中...';
    progressPanel.style.display = 'block';
    isClassificationRunning = true;
    
    updateStepIndicator(0);
    progressBar.style.width = '1%';
    progressText.textContent = '正在启动Python环境...';
    
    try {
        if (classifyEventSource) {
            classifyEventSource.close();
        }
        
        const taskId = Date.now().toString();
        
        classifyEventSource = new EventSource('/api/classify_stream?task_id=' + taskId);
        console.log('[SSE] Connection established, TaskID=' + taskId);
        
        const objectIds = selected.map(o => ({
            source_id: o.source_id,
            healpix_id: o.healpix_id,
            ra: o.ra,
            dec: o.dec
        }));
        
        fetch('/api/classify_objects?task_id=' + taskId, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ objects: objectIds })
        }).then(() => console.log('[POST] Classification started'))
          .catch(e => console.error('[POST] Start failed:', e));
        
        classifyEventSource.onmessage = async (event) => {
            try {
                const status = JSON.parse(event.data);
                
                const bar = document.getElementById('progressBar');
                const text = document.getElementById('progressText');
                
                if (status.percent > 0 && bar && text) {
                    bar.style.width = status.percent + '%';
                    text.textContent = status.message || `${status.percent}%`;
                    
                    if (status.step === 'extract' || status.percent < 33) {
                        updateStepIndicator(0);
                    } else if (status.step === 'feature' || status.percent < 66) {
                        updateStepIndicator(1);
                    } else {
                        updateStepIndicator(2);
                    }
                }
                
                if (status.percent >= 100 || status.step === 'done' || status.message?.includes('完成') || status.message?.includes('Done')) {
                    classifyEventSource.close();
                    classifyEventSource = null;
                    
                    await new Promise(r => setTimeout(r, 1000));
                    
                    const resultRes = await fetch('/api/classify_results?limit=500');
                    const data = await resultRes.json();
                    
                    if (data.results && data.results.length > 0) {
                        displayResults(data.results);
                        updateStepIndicator(3); 
                        showToast(tMsg('msg_classify_success', data.results.length), 'success');
                    }
                    
                    finishClassification();
                }
            } catch (e) {
                console.error('SSE Error:', e);
            }
        };
        
        classifyEventSource.onerror = (e) => {
            console.log('SSE connection closed or error');
            if (classifyEventSource) {
                classifyEventSource.close();
                classifyEventSource = null;
            }
        };
        
    } catch (error) {
        showToast('分类失败: ' + error.message, 'error');
        finishClassification();
    }
};

function finishClassification() {
    isClassificationRunning = false;
    const btn = document.getElementById('classifyBtn');
    const panel = document.getElementById('progressPanel');
    if (btn) {
        btn.disabled = false;
        btn.innerHTML = '<i class="fas fa-play"></i> 开始分类';
        }
    setTimeout(() => { if (panel) panel.style.display = 'none'; }, 2000);
}

window.stopClassification = async function() {
    try {
        const stopBtn = document.getElementById('stopBtnRight');
        const classifyBtn = document.getElementById('classifyBtn');
        
        if (stopBtn) {
            stopBtn.disabled = true;
            stopBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> 停止中...';
        }
        
        if (classifyEventSource) {
            classifyEventSource.close();
            classifyEventSource = null;
        }
        
        await fetch('/api/classify_stop');
        isClassificationRunning = false;
        
        showToast(tMsg('msg_classify_stopped'), 'success');
        
        if (classifyBtn) {
            classifyBtn.disabled = false;
            classifyBtn.innerHTML = '<i class="fas fa-play"></i> 开始分类';
        }
        if (stopBtn) {
            stopBtn.disabled = false;
            stopBtn.innerHTML = '<i class="fas fa-stop"></i> 停止分类';
        }
        
        setTimeout(() => {
            const panel = document.getElementById('progressPanel');
            if (panel) panel.style.display = 'none';
        }, 1000);
    } catch (error) {
        showToast('停止失败: ' + error.message, 'error');
    }
};

function updateStepIndicator(currentStep) {
    for (let i = 1; i <= 3; i++) {
        const circle = document.getElementById('step' + i);
        if (!circle) continue;
        
        circle.style.background = 'var(--bg)';
        circle.style.borderColor = 'var(--border)';
        circle.style.color = 'var(--text-secondary)';
        
        if (i - 1 < currentStep) {
            circle.style.background = '#059669';
            circle.style.borderColor = '#059669';
            circle.style.color = 'white';
        } else if (i - 1 === currentStep) {
            circle.style.background = '#d97706';
            circle.style.borderColor = '#d97706';
            circle.style.color = 'white';
        }
    }
}

function displayResults(results) {
    const tbody = document.getElementById('resultsBody');
    if (!tbody) return;
    
    tbody.innerHTML = results.map(res => {
        const scoreColor = res.confidence > 0.8 ? 'var(--success)' : res.confidence > 0.6 ? 'var(--warning)' : 'var(--danger)';
        const classColor = getClassColor(res.prediction);
        return `
            <tr onclick="viewObject('${res.source_id}')">
                <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.85rem;">${res.source_id}</td>
                <td style="color: var(--text-secondary); font-size: 0.85rem;">${res.ra?.toFixed(2)}°, ${res.dec?.toFixed(2)}°</td>
                <td><span class="class-badge" style="background: ${classColor};">${formatClassName(res.prediction)}</span></td>
                <td style="font-weight: 600; color: ${scoreColor}; font-family: 'JetBrains Mono', monospace;">${res.confidence?.toFixed(4) || '-'}</td>
            </tr>
        `;
    }).join('');
}

function formatClassName(cls) {
    return cls === 'RRC' ? 'RRc' : cls;
}

function getClassColor(cls) {
    const colors = {
        'RRAB': '#ef4444', 'RRC': '#f97316', 'DSCT': '#f59e0b', 'CEP': '#eab308',
        'M': '#84cc16', 'SR': '#22c55e', 'EA': '#14b8a6', 'EW': '#06b6d4',
        'ROT': '#3b82f6', 'Non-var': '#6b7280', 'Variable': '#8b5cf6'
    };
    return colors[cls] || '#64748b';
}

let highlightedObjectId = null;
let currentConeRegion = null;
let allSearchRegions = [];  // 存储所有检索区域边缘

window.toggleSkyMapMode = function() {
    is3DMode = !is3DMode;
    if (currentObjects.length > 0) {
        plotSkyMap(currentObjects, currentConeRegion);
    }
};

function plotSkyMap(objects, coneRegion = null, regions = null) {
    currentConeRegion = coneRegion;
    if (regions !== null) allSearchRegions = regions;
    const placeholder = document.getElementById('skyMapPlaceholder');
    if (placeholder) placeholder.style.display = 'none';
    
    if (is3DMode) {
        plotSkyMap3D(objects, coneRegion);
    } else {
        plotSkyMap2D(objects, coneRegion);
    }
}

function plotSkyMap3D(objects, coneRegion) {
    const container = document.getElementById('skyMapContainer');
    if (!container) return;
    
    // 如果 Three.js 未加载，回退到 2D 模式
    if (typeof THREE === 'undefined') {
        console.warn('Three.js not loaded, falling back to 2D mode');
        plotSkyMap2D(objects, coneRegion);
        return;
    }
    
    const canvas = document.getElementById('skyMapCanvas');
    if (canvas) canvas.style.display = 'none';
    
    let skyMap3DEl = document.getElementById('skyMap3D');
    if (skyMap3DEl) skyMap3DEl.innerHTML = '';
    else {
        skyMap3DEl = document.createElement('div');
        skyMap3DEl.id = 'skyMap3D';
        skyMap3DEl.style.cssText = 'width: 100%; height: 100%; background: #ffffff;';
        container.appendChild(skyMap3DEl);
    }
    skyMap3DEl.style.display = 'block';
    
    const width = skyMap3DEl.clientWidth || 800;
    const height = skyMap3DEl.clientHeight || 480;
    
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0xffffff);
    
    const camera = new THREE.PerspectiveCamera(75, width / height, 0.1, 1000);
    camera.position.set(0, 0, 100);
    
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setClearColor(0xffffff, 1);
    renderer.setSize(width, height);
    skyMap3DEl.appendChild(renderer.domElement);
    
    const controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    
    const sphereGeom = new THREE.SphereGeometry(50, 32, 32);
    const sphereMat = new THREE.MeshBasicMaterial({ color: 0xd1d5db, wireframe: true });
    scene.add(new THREE.Mesh(sphereGeom, sphereMat));
    
    const equatorPoints = [];
    for (let i = 0; i <= 64; i++) {
        const theta = (i / 64) * Math.PI * 2;
        equatorPoints.push(new THREE.Vector3(50 * Math.cos(theta), 0, 50 * Math.sin(theta)));
    }
    const equatorGeom = new THREE.BufferGeometry().setFromPoints(equatorPoints);
    scene.add(new THREE.Line(equatorGeom, new THREE.LineBasicMaterial({ color: 0x6366f1 })));
    
    const objectMarkers = [];
    objects.forEach(obj => {
        if (obj.ra == null || obj.dec == null) return;
        
        const phi = (90 - obj.dec) * Math.PI / 180;
        const theta = obj.ra * Math.PI / 180;
        const x = 51 * Math.sin(phi) * Math.cos(theta);
        const y = 51 * Math.cos(phi);
        const z = 51 * Math.sin(phi) * Math.sin(theta);
        
        const isHighlighted = highlightedObjectId && String(obj.source_id) === String(highlightedObjectId);
        const geometry = new THREE.SphereGeometry(0.25, 8, 8);
        const material = new THREE.MeshBasicMaterial({ color: isHighlighted ? 0xef4444 : 0x3b82f6 });
        const marker = new THREE.Mesh(geometry, material);
        marker.position.set(x, y, z);
        marker.userData = obj;
        scene.add(marker);
        objectMarkers.push(marker);
    });
    
    // 绘制所有检索区域边缘
    const regionColors = [0xf59e0b, 0x06b6d4, 0xa855f7, 0xef4444, 0x22c55e, 0x3b82f6];
    allSearchRegions.forEach((region, idx) => {
        const color = regionColors[idx % regionColors.length];
        if (region.type === 'cone') {
            const centerPhi = (90 - region.dec) * Math.PI / 180;
            const centerTheta = region.ra * Math.PI / 180;
            const radiusRad = region.radius * Math.PI / 180;
            
            const circlePoints = [];
            for (let i = 0; i <= 64; i++) {
                const angle = (i / 64) * 2 * Math.PI;
                const phi = centerPhi + radiusRad * Math.cos(angle);
                const theta = centerTheta + radiusRad * Math.sin(angle) / Math.sin(centerPhi);
                const x = 51.5 * Math.sin(phi) * Math.cos(theta);
                const y = 51.5 * Math.cos(phi);
                const z = 51.5 * Math.sin(phi) * Math.sin(theta);
                circlePoints.push(new THREE.Vector3(x, y, z));
            }
            const circleGeom = new THREE.BufferGeometry().setFromPoints(circlePoints);
            scene.add(new THREE.Line(circleGeom, new THREE.LineBasicMaterial({ color })));
        } else if (region.type === 'rect') {
            const corners = [
                {ra: region.raMin, dec: region.decMin},
                {ra: region.raMax, dec: region.decMin},
                {ra: region.raMax, dec: region.decMax},
                {ra: region.raMin, dec: region.decMax},
                {ra: region.raMin, dec: region.decMin}
            ];
            const rectPoints = corners.map(c => {
                const phi = (90 - c.dec) * Math.PI / 180;
                const theta = c.ra * Math.PI / 180;
                return new THREE.Vector3(
                    51.5 * Math.sin(phi) * Math.cos(theta),
                    51.5 * Math.cos(phi),
                    51.5 * Math.sin(phi) * Math.sin(theta)
                );
            });
            const rectGeom = new THREE.BufferGeometry().setFromPoints(rectPoints);
            scene.add(new THREE.Line(rectGeom, new THREE.LineBasicMaterial({ color })));
        }
    });
    
    const raycaster = new THREE.Raycaster();
    const mouse = new THREE.Vector2();
    
    renderer.domElement.addEventListener('click', function(event) {
        const rect = renderer.domElement.getBoundingClientRect();
        mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
        mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
        raycaster.setFromCamera(mouse, camera);
        
        const intersects = raycaster.intersectObjects(objectMarkers);
        if (intersects.length > 0) {
            const clickedObject = intersects[0].object.userData;
            highlightedObjectId = clickedObject.source_id;
            
            plotSkyMap(currentObjects, currentConeRegion);
            
            updateMetadata(clickedObject, clickedObject.data_count);
            document.getElementById('objectInfo').textContent = 'Source ID: ' + clickedObject.source_id;
            
            loadLightcurveForObject(clickedObject.table_name);
        }
    });
    
    function animate() {
        requestAnimationFrame(animate);
        controls.update();
        renderer.render(scene, camera);
    }
    animate();
    
    skyMap3D = { scene, camera, renderer, controls };
}

function plotSkyMap2D(objects, coneRegion) {
    const canvas = document.getElementById('skyMapCanvas');
    if (!canvas) return;
    
    const skyMap3DEl = document.getElementById('skyMap3D');
    if (skyMap3DEl) skyMap3DEl.style.display = 'none';
    canvas.style.display = 'block';
    
    canvas.width = canvas.parentElement.clientWidth || 800;
    canvas.height = canvas.parentElement.clientHeight || 480;
    
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;
    
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, width, height);
    
    const margin = 40;
    const plotWidth = width - 2 * margin;
    const plotHeight = height - 2 * margin;
    
    const ras = objects.map(o => o.ra).filter(v => v != null);
    const decs = objects.map(o => o.dec).filter(v => v != null);
    const minRa = Math.min(...ras) - 0.5;
    const maxRa = Math.max(...ras) + 0.5;
    const minDec = Math.min(...decs) - 0.5;
    const maxDec = Math.max(...decs) + 0.5;
    
    ctx.strokeStyle = '#e5e7eb';
    ctx.lineWidth = 0.5;
    for (let i = 0; i <= 10; i++) {
        const ra = minRa + (maxRa - minRa) * i / 10;
        const x = margin + (ra - minRa) / (maxRa - minRa) * plotWidth;
        ctx.beginPath();
        ctx.moveTo(x, margin);
        ctx.lineTo(x, margin + plotHeight);
        ctx.stroke();
        
        ctx.fillStyle = '#6b7280';
        ctx.font = '11px sans-serif';
        ctx.fillText(ra.toFixed(1) + '°', x - 15, margin + plotHeight + 18);
    }
    for (let i = 0; i <= 10; i++) {
        const dec = minDec + (maxDec - minDec) * i / 10;
        const y = margin + (maxDec - dec) / (maxDec - minDec) * plotHeight;
        ctx.beginPath();
        ctx.moveTo(margin, y);
        ctx.lineTo(margin + plotWidth, y);
        ctx.stroke();
        
        ctx.fillStyle = '#6b7280';
        ctx.fillText(dec.toFixed(1) + '°', 5, y + 4);
    }
    
    ctx.strokeStyle = '#9ca3af';
    ctx.lineWidth = 1;
    ctx.strokeRect(margin, margin, plotWidth, plotHeight);
    
    // 绘制所有检索区域边缘
    const regionColors2D = ['#f59e0b', '#06b6d4', '#a855f7', '#ef4444', '#22c55e', '#3b82f6'];
    allSearchRegions.forEach((region, idx) => {
        const color = regionColors2D[idx % regionColors2D.length];
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.setLineDash(idx === allSearchRegions.length - 1 ? [] : [6, 4]);  // 最新的实线，旧的虚线
        
        if (region.type === 'cone') {
            const cx = margin + (region.ra - minRa) / (maxRa - minRa) * plotWidth;
            const cy = margin + (maxDec - region.dec) / (maxDec - minDec) * plotHeight;
            const r = (region.radius / (maxRa - minRa)) * plotWidth;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
            ctx.stroke();
        } else if (region.type === 'rect') {
            const x1 = margin + (region.raMin - minRa) / (maxRa - minRa) * plotWidth;
            const x2 = margin + (region.raMax - minRa) / (maxRa - minRa) * plotWidth;
            const y1 = margin + (maxDec - region.decMax) / (maxDec - minDec) * plotHeight;
            const y2 = margin + (maxDec - region.decMin) / (maxDec - minDec) * plotHeight;
            ctx.beginPath();
            ctx.rect(x1, y1, x2 - x1, y2 - y1);
            ctx.stroke();
        }
    });
    ctx.setLineDash([]);  // 恢复实线
    
    const objectPoints = [];
    objects.forEach(obj => {
        if (obj.ra == null || obj.dec == null) return;
        
        const x = margin + (obj.ra - minRa) / (maxRa - minRa) * plotWidth;
        const y = margin + (maxDec - obj.dec) / (maxDec - minDec) * plotHeight;
        const isHighlighted = highlightedObjectId && String(obj.source_id) === String(highlightedObjectId);
        
        ctx.fillStyle = isHighlighted ? '#ef4444' : '#3b82f6';
        ctx.beginPath();
        ctx.arc(x, y, 2, 0, 2 * Math.PI);
        ctx.fill();
        
        objectPoints.push({ x, y, obj });
    });
    
    canvas.onclick = null;
    canvas.onclick = function(event) {
        const rect = canvas.getBoundingClientRect();
        const clickX = event.clientX - rect.left;
        const clickY = event.clientY - rect.top;
        
        for (let point of objectPoints) {
            const distance = Math.sqrt(Math.pow(clickX - point.x, 2) + Math.pow(clickY - point.y, 2));
            if (distance <= 10) {
                highlightedObjectId = point.obj.source_id;
                plotSkyMap(currentObjects, currentConeRegion);
                updateMetadata(point.obj, point.obj.data_count);
                document.getElementById('objectInfo').textContent = 'Source ID: ' + point.obj.source_id;
                loadLightcurveForObject(point.obj.table_name);
                break;
            }
        }
    };
    
    canvas.onmousemove = function(event) {
        const rect = canvas.getBoundingClientRect();
        const mouseX = event.clientX - rect.left;
        const mouseY = event.clientY - rect.top;
        
        let hovered = false;
        for (let point of objectPoints) {
            const distance = Math.sqrt(Math.pow(mouseX - point.x, 2) + Math.pow(mouseY - point.y, 2));
            if (distance <= 10) {
                canvas.style.cursor = 'pointer';
                hovered = true;
                break;
            }
        }
        if (!hovered) canvas.style.cursor = 'default';
    };
}

async function loadLightcurveForObject(tableName) {
    if (!tableName) {
        console.error('No table_name provided');
        return;
    }
    try {
        const response = await fetch(`/api/lightcurve/${tableName}`);
        const data = await response.json();
        if (data.data && data.data.length > 0) {
            plotLightcurve(data);
            document.getElementById('chartPlaceholder').style.display = 'none';
            document.getElementById('lightcurveChart').style.display = 'block';
        } else {
            console.log('No lightcurve data returned');
        }
    } catch (error) {
        console.error('Failed to load lightcurve:', error);
    }
}

function plotLightcurve(data) {
    const placeholder = document.getElementById('chartPlaceholder');
    if (placeholder) placeholder.style.display = 'none';
    
    const canvas = document.getElementById('lightcurveChart');
    if (!canvas) return;
    canvas.style.display = 'block';
    
    if (lightcurveChart) lightcurveChart.destroy();
    
    // 波段颜色配置
    const bandColors = {
        'G': { border: '#22c55e', bg: '#22c55e' },      // 绿色
        'BP': { border: '#3b82f6', bg: '#3b82f6' },     // 蓝色
        'RP': { border: '#ef4444', bg: '#ef4444' },     // 红色
        'g': { border: '#22c55e', bg: '#22c55e' },
        'r': { border: '#ef4444', bg: '#ef4444' },
        'i': { border: '#a855f7', bg: '#a855f7' },      // 紫色
        'z': { border: '#f97316', bg: '#f97316' },      // 橙色
        'u': { border: '#06b6d4', bg: '#06b6d4' },      // 青色
        'default': { border: '#6b7280', bg: '#6b7280' } // 灰色
    };
    
    // 按波段分组数据
    const bandGroups = {};
    data.data.forEach(d => {
        const band = d.band || 'Unknown';
        if (!bandGroups[band]) {
            bandGroups[band] = [];
        }
        bandGroups[band].push({
            x: new Date(d.ts).getTime(),
            y: d.mag,
            mag_err: d.mag_err,
            flux: d.flux,
            flux_err: d.flux_err
        });
    });
    
    // 为每个波段创建 dataset
    const datasets = Object.keys(bandGroups).map(band => {
        const colors = bandColors[band] || bandColors['default'];
        return {
            label: band,
            data: bandGroups[band],
            borderColor: colors.border,
            backgroundColor: colors.bg,
            pointRadius: 3,
            pointHoverRadius: 6
        };
    });
    
    const ctx = canvas.getContext('2d');
    lightcurveChart = new Chart(ctx, {
        type: 'scatter',
        data: { datasets },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { 
                    display: datasets.length > 1,
                    position: 'top',
                    labels: {
                        usePointStyle: true,
                        padding: 15
                    }
                },
                tooltip: {
                    callbacks: {
                        label: ctx => {
                            const point = ctx.raw;
                            let label = `${ctx.dataset.label}: ${point.y.toFixed(4)} mag`;
                            if (point.mag_err) label += ` ±${point.mag_err.toFixed(4)}`;
                            return label;
                        }
                    }
                }
            },
            scales: {
                x: {
                    type: 'time',
                    time: { unit: 'month', displayFormats: { month: 'yyyy-MM' } },
                    title: { display: true, text: '时间' }
                },
                y: { 
                    title: { display: true, text: '星等' },
                    reverse: false  // 星等值越小越亮，如需反转设为 true
                }
            }
        }
    });
}

window.downloadObjectList = function() {
    if (selectedObjects.length === 0) {
        showToast(tMsg('msg_list_empty'), 'error');
        return;
    }
    
    let csvContent = "data:text/csv;charset=utf-8,";
    csvContent += "source_id,ra,dec,healpix_id,object_class,band\n";
    
    selectedObjects.forEach(obj => {
        const row = [
            obj.source_id,
            obj.ra,
            obj.dec,
            obj.healpix_id,
            formatClassName(obj.object_class) || 'UNKNOWN',
            obj.band || 'Unknown'
        ].join(",");
        csvContent += row + "\n";
    });
    
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", "object_list.csv");
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
};

window.downloadLightcurve = function() {
    if (!currentLightcurveData || currentLightcurveData.length === 0) {
        showToast(tMsg('msg_no_lc_data_download'), 'error');
        return;
    }
    
    let csvContent = "data:text/csv;charset=utf-8,";
    csvContent += "timestamp,mag,mag_err,flux,flux_err\n";
    
    currentLightcurveData.forEach(point => {
        const row = [
            point.ts,
            point.mag,
            point.mag_err,
            point.flux,
            point.flux_err
        ].join(",");
        csvContent += row + "\n";
    });
    
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    const sourceId = document.getElementById('objectInfo').textContent.replace('Source ID: ', '') || 'lightcurve';
    link.setAttribute("download", `lightcurve_${sourceId}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
};

function showToast(message, type = 'success') {
    const existing = document.querySelector('.toast');
    if (existing) existing.remove();
    
    const toast = document.createElement('div');
    toast.className = 'toast ' + type;
    toast.innerHTML = `<i class="fas fa-${type === 'success' ? 'check-circle' : 'exclamation-circle'}"></i> ${message}`;
    document.body.appendChild(toast);
    
    setTimeout(() => toast.remove(), 3000);
}

// Helper function for localized messages with parameters
function tMsg(key, ...args) {
    return typeof t === 'function' ? t(key, ...args) : key;
}

// ==================== 数据导入功能 ====================

let importEventSource = null;

window.startCatalogImport = async function() {
    const path = document.getElementById('catalogPath').value.trim();
    const coordsPath = document.getElementById('coordsPath').value.trim();
    const dbName = document.getElementById('importDbName').value.trim();
    const nside = parseInt(document.getElementById('importNside').value) || 64;
    
    if (!path) {
        showToast(tMsg('msg_enter_catalog_path'), 'error');
        return;
    }
    if (!coordsPath) {
        showToast(tMsg('msg_enter_coords_path'), 'error');
        return;
    }
    
    startImportTask('catalog', path, coordsPath, dbName, nside);
};

window.startLightcurveImport = async function() {
    const path = document.getElementById('lightcurvePath').value.trim();
    const coordsPath = document.getElementById('coordsPath').value.trim();
    const dbName = document.getElementById('importDbName').value.trim();
    const nside = parseInt(document.getElementById('importNside').value) || 64;
    
    if (!path) {
        showToast(tMsg('msg_enter_lc_path'), 'error');
        return;
    }
    if (!coordsPath) {
        showToast(tMsg('msg_enter_coords_path'), 'error');
        return;
    }
    
    startImportTask('lightcurve', path, coordsPath, dbName, nside);
};

async function startImportTask(type, path, coordsPath, dbName, nside) {
    document.getElementById('importProgressCard').style.display = 'block';
    document.getElementById('importProgressText').textContent = '正在启动导入...';
    document.getElementById('importProgressPercent').textContent = '0%';
    document.getElementById('importProgressBar').style.width = '0%';
    document.getElementById('importStats').innerHTML = '';
    
    // Get threads and vgroups from settings
    const threads = parseInt(document.getElementById('configThreads')?.value) || 16;
    const vgroups = parseInt(document.getElementById('configVgroups')?.value) || 32;
    
    try {
        if (importEventSource) {
            importEventSource.close();
            importEventSource = null;
        }

        // 效仿分类功能：先建立 SSE 连接，再发送 POST
        console.log('[SSE Import] Creating EventSource first...');
        importEventSource = new EventSource('/api/import/stream');
        
        // 然后发送 POST 启动导入（不等待）
        fetch('/api/import/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ type, path, coords_path: coordsPath, db_name: dbName, nside, threads, vgroups })
        }).then(r => r.json()).then(result => {
            if (result.success) {
                console.log('[POST] Import started');
                showToast(tMsg('msg_import_started'), 'success');
            } else {
                console.error('[POST] Import failed:', result.error);
                showToast(tMsg('msg_import_start_failed') + ': ' + result.error, 'error');
            }
        }).catch(e => {
            console.error('[POST] Import error:', e);
            showToast(tMsg('msg_import_start_failed') + ': ' + e.message, 'error');
        });
        
        importEventSource.onopen = () => {
            console.log('[SSE Import] Connection opened');
        };
        
        importEventSource.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                
                // 忽略 idle 状态（等待实际进度）
                if (data.status === 'idle') {
                    return;
                }
                
                // 恢复普通进度条
                const pBar = document.getElementById('importProgressBar');
                if(pBar && pBar.parentElement) pBar.parentElement.style.display = 'block';
                document.getElementById('importProgressText').style.display = 'block';
                document.getElementById('importProgressPercent').style.display = 'block';
                
                // 显示终端日志 (用户要求)
                let term = document.getElementById('importTerminal');
                if (!term) {
                    const container = document.getElementById('importProgressCard');
                    term = document.createElement('pre');
                    term.id = 'importTerminal';
                    term.style.cssText = 'background: #1e1e1e; color: #d4d4d4; padding: 10px; border-radius: 6px; font-family: "JetBrains Mono", monospace; font-size: 12px; height: 320px; overflow-y: auto; white-space: pre-wrap; margin-top: 10px; border: 1px solid var(--border);';
                    container.appendChild(term);
                }
                
                if (data.log) {
                    // 处理 \r (模拟终端覆盖)
                    const cleanLog = data.log.split('\n').map(line => {
                        const parts = line.split('\r');
                        return parts[parts.length - 1];
                    }).join('\n');
                    term.textContent = cleanLog;
                    term.scrollTop = term.scrollHeight;
                }
                
                document.getElementById('importProgressText').textContent = data.message || '进行中...';
                document.getElementById('importProgressPercent').textContent = data.percent + '%';
                if (pBar) pBar.style.width = data.percent + '%';
                
                // 更新步骤
                if (data.message && (data.message.includes('建表') || data.message.includes('Creating tables'))) {
                    updateImportStep(0);
                } else if (data.status === 'completed' || data.percent >= 100) {
                    updateImportStep(2);
                } else {
                    updateImportStep(1);
                }
                
                if (data.stats) {
                    document.getElementById('importStats').innerHTML = `
                        <div>已处理文件: ${data.stats.processed_files || 0}</div>
                        <div>已插入记录: ${data.stats.inserted_records || 0}</div>
                        <div>已创建表: ${data.stats.created_tables || 0}</div>
                        <div>耗时: ${data.stats.elapsed_time || '0s'}</div>
                    `;
                }
                
                if (data.percent >= 100 || data.status === 'completed' || data.status === 'stopped') {
                    importEventSource.close();
                    importEventSource = null;
                    
                    if (data.status === 'stopped') {
                        showToast(tMsg('msg_import_stopped'), 'warning');
                    } else {
                        showToast(tMsg('msg_import_complete'), 'success');
                        updateImportStep(2);
                        
                        // 刷新待分类天体数量并自动启动分类
                        setTimeout(async () => {
                            const count = await window.refreshPendingCount();
                            if (count > 0) {
                                showToast(tMsg('msg_auto_detect_hint', count), 'success');
                                
                                // 检查是否启用自动分类
                                const autoClassifyCheckbox = document.getElementById('autoClassifyAfterImport');
                                if (autoClassifyCheckbox && autoClassifyCheckbox.checked) {
                                    // 延迟1秒后自动开始分类
                                    setTimeout(() => {
                                        showToast(tMsg('msg_auto_starting'), 'success');
                                        window.startAutoClassify(false);
                                    }, 1000);
                                }
                            }
                        }, 500);
                    }
                }
            } catch (e) {
                console.error('SSE Error:', e);
            }
        };
        
        importEventSource.onerror = (e) => {
            console.log('SSE connection closed');
            if (importEventSource) {
                importEventSource.close();
                importEventSource = null;
            }
        };
        
    } catch (e) {
        showToast('启动导入失败: ' + e.message, 'error');
    }
}

function updateImportStep(currentStep) {
    for (let i = 1; i <= 3; i++) {
        const circle = document.getElementById('importStep' + i);
        if (!circle) continue;
        
        circle.style.background = 'var(--bg)';
        circle.style.borderColor = 'var(--border)';
        circle.style.color = 'var(--text-secondary)';
        
        if (i - 1 < currentStep) {
            circle.style.background = '#059669';
            circle.style.borderColor = '#059669';
            circle.style.color = 'white';
        } else if (i - 1 === currentStep) {
            circle.style.background = '#d97706';
            circle.style.borderColor = '#d97706';
            circle.style.color = 'white';
        }
    }
}

window.stopImport = async function() {
    try {
        // 先发送停止请求
        const response = await fetch('/api/import/stop', { method: 'POST' });
        const result = await response.json();
        
        // 关闭 SSE 连接
        if (importEventSource) {
            importEventSource.close();
            importEventSource = null;
        }
        
        if (result.success) {
            showToast(tMsg('msg_import_stopped'), 'warning');
            // 更新 UI
            document.getElementById('importProgressText').textContent = tMsg('msg_import_stopped');
            document.getElementById('importProgressPercent').textContent = '-';
            const pBar = document.getElementById('importProgressBar');
            if (pBar) pBar.style.width = '0%';
            updateImportStep(0);
        } else {
            showToast(tMsg('msg_stop_failed') + ': ' + (result.error || ''), 'error');
        }
    } catch (e) {
        showToast(tMsg('msg_stop_failed') + ': ' + e.message, 'error');
    }
};

// ==================== 数据库管理 ====================

window.refreshDbList = async function() {
    try {
        const resp = await fetch('/api/databases');
        const data = await resp.json();
        
        const select = document.getElementById('dropDbSelect');
        select.innerHTML = '<option value="">' + tMsg('select_database') + '</option>';
        
        if (data.databases && data.databases.length > 0) {
            for (const db of data.databases) {
                // 跳过系统库
                if (db === 'information_schema' || db === 'performance_schema') continue;
                const opt = document.createElement('option');
                opt.value = db;
                opt.textContent = db;
                select.appendChild(opt);
            }
            showToast(tMsg('msg_db_refreshed', data.databases.length - 2), 'success');
        } else {
            showToast(tMsg('msg_no_user_db'), 'warning');
        }
    } catch (e) {
        showToast(tMsg('msg_refresh_failed') + ': ' + e.message, 'error');
    }
};

window.dropSelectedDatabase = async function() {
    const select = document.getElementById('dropDbSelect');
    const dbName = select.value;
    
    if (!dbName) {
        showToast(tMsg('msg_select_db_delete'), 'warning');
        return;
    }
    
    if (!confirm(tMsg('msg_confirm_delete', dbName))) {
        return;
    }
    
    try {
        const resp = await fetch('/api/database/drop', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ db_name: dbName })
        });
        const data = await resp.json();
        
        if (data.success) {
            showToast(tMsg('msg_db_deleted', dbName), 'success');
            window.refreshDbList();
        } else {
            showToast(tMsg('msg_delete_failed') + ': ' + data.error, 'error');
        }
    } catch (e) {
        showToast(tMsg('msg_delete_failed') + ': ' + e.message, 'error');
    }
};

// ==================== 自动分类功能 ====================

let autoClassifyEventSource = null;

window.refreshPendingCount = async function(runCheck = true) {
    const countEl = document.getElementById('pendingClassifyCount');
    const hint = document.getElementById('pendingClassifyHint');
    const refreshBtn = document.getElementById('refreshPendingBtn');
    
    // 优先使用界面输入的数据库名，否则从配置获取
    let dbName = document.getElementById('importDbName')?.value?.trim() || '';
    
    try {
        // 显示检测中状态
        if (runCheck) {
            if (countEl) countEl.innerHTML = '<i class="fas fa-spinner fa-spin"></i>';
            if (hint) hint.textContent = tMsg('msg_detection_querying');
            if (refreshBtn) {
                refreshBtn.disabled = true;
                refreshBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> ' + tMsg('query');
            }
            
            // 触发检测（如果没填数据库名，让后端用默认配置）
            const checkResp = await fetch('/api/auto_classify/check', { 
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ db_name: dbName })
            });
            const checkData = await checkResp.json();
            
            if (!checkData.success) {
                showToast(tMsg('msg_detection_failed') + ': ' + (checkData.error || ''), 'error');
            } else {
                // 使用返回的数据库名（可能是默认配置）
                dbName = checkData.db_name || dbName;
            }
        }
        
        // 获取数量
        const resp = await fetch(`/api/auto_classify/candidates?db_name=${encodeURIComponent(dbName)}`);
        const data = await resp.json();
        const count = data.count || 0;
        dbName = data.db_name || dbName;
        
        if (countEl) countEl.textContent = count.toLocaleString();
        
        const batchSize = parseInt(document.getElementById('autoClassifyBatchSize')?.value) || 5000;
        if (hint) {
            if (count > 0) {
                const batches = Math.ceil(count / batchSize);
                hint.innerHTML = `<strong>${dbName}</strong>: ` + tMsg('msg_queue_info', count.toLocaleString(), batches);
            } else {
                hint.innerHTML = `<strong>${dbName}</strong>: ` + tMsg('msg_queue_empty');
            }
        }
        
        const startBtn = document.getElementById('startAutoClassifyBtn');
        if (startBtn) {
            startBtn.disabled = count === 0;
        }
        
        return count;
    } catch (e) {
        console.error('Failed to refresh pending count:', e);
        if (countEl) countEl.textContent = '?';
        if (hint) hint.textContent = tMsg('msg_detection_failed');
        return 0;
    } finally {
        if (refreshBtn) {
            refreshBtn.disabled = false;
            refreshBtn.innerHTML = '<i class="fas fa-sync-alt"></i> ' + tMsg('query');
        }
    }
};

// 兼容旧名称
window.refreshCandidateCount = window.refreshPendingCount;

// 数据库名切换时，刷新待分类数量（不运行检测，只读取已有队列）
window.onDbNameChange = async function() {
    const countEl = document.getElementById('pendingClassifyCount');
    const hint = document.getElementById('pendingClassifyHint');
    const dbName = document.getElementById('importDbName')?.value?.trim() || '';
    
    if (!dbName) {
        if (countEl) countEl.textContent = '-';
        if (hint) hint.textContent = tMsg('msg_enter_db_name');
        return;
    }
    
    try {
        if (countEl) countEl.innerHTML = '<i class="fas fa-spinner fa-spin"></i>';
        
        // 只读取已有队列，不触发新检测
        const resp = await fetch(`/api/auto_classify/candidates?db_name=${encodeURIComponent(dbName)}`);
        const data = await resp.json();
        const count = data.count || 0;
        
        if (countEl) countEl.textContent = count.toLocaleString();
        
        const batchSize = parseInt(document.getElementById('autoClassifyBatchSize')?.value) || 5000;
        if (hint) {
            if (count > 0) {
                const batches = Math.ceil(count / batchSize);
                hint.innerHTML = `<strong>${dbName}</strong>: ` + tMsg('msg_queue_info', count.toLocaleString(), batches);
            } else {
                hint.innerHTML = `<strong>${dbName}</strong>: ` + tMsg('msg_click_query');
            }
        }
        
        const startBtn = document.getElementById('startAutoClassifyBtn');
        if (startBtn) startBtn.disabled = count === 0;
        
    } catch (e) {
        console.error('Failed to get candidate count:', e);
        if (countEl) countEl.textContent = '?';
    }
};

window.startAutoClassify = async function(resume = false) {
    const batchSize = parseInt(document.getElementById('autoClassifyBatchSize').value) || 5000;
    
    try {
        document.getElementById('autoClassifyProgressCard').style.display = 'block';
        document.getElementById('autoClassifyProgressText').textContent = '启动中...';
        document.getElementById('autoClassifyProgressPercent').textContent = '0%';
        document.getElementById('autoClassifyProgressBar').style.width = '0%';
        
        // 先建立 SSE 连接
        if (autoClassifyEventSource) {
            autoClassifyEventSource.close();
        }
        
        autoClassifyEventSource = new EventSource('/api/auto_classify/stream');
        
        autoClassifyEventSource.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                
                if (data.status === 'idle') return;
                
                document.getElementById('autoClassifyProgressText').textContent = data.message || '处理中...';
                document.getElementById('autoClassifyProgressPercent').textContent = data.percent + '%';
                document.getElementById('autoClassifyProgressBar').style.width = data.percent + '%';
                
                // 显示批次统计
                if (data.current_batch) {
                    document.getElementById('autoClassifyStats').innerHTML = `
                        <div>当前批次: ${data.current_batch}/${data.total_batches}</div>
                        <div>批次进度: ${data.batch_progress || 0}% (${data.processed || 0}/${data.batch_total || 0})</div>
                        <div>已更新: ${data.updated || 0} 个</div>
                    `;
                }
                
                // 完成或暂停
                if (data.status === 'completed' || data.status === 'paused' || data.status === 'error') {
                    autoClassifyEventSource.close();
                    autoClassifyEventSource = null;
                    
                    if (data.status === 'completed') {
                        showToast(tMsg('msg_auto_classify_complete'), 'success');
                        window.refreshCandidateCount();
                    } else if (data.status === 'paused') {
                        showToast(tMsg('msg_auto_classify_paused'), 'success');
                    } else {
                        showToast(tMsg('msg_auto_classify_error') + ': ' + data.message, 'error');
                    }
                }
            } catch (e) {
                console.error('Auto classify SSE error:', e);
            }
        };
        
        autoClassifyEventSource.onerror = () => {
            if (autoClassifyEventSource) {
                autoClassifyEventSource.close();
                autoClassifyEventSource = null;
            }
        };
        
        // 发送启动请求
        const dbName = document.getElementById('importDbName')?.value?.trim() || '';
        const resp = await fetch('/api/auto_classify/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ resume, batch_size: batchSize, db_name: dbName })
        });
        const result = await resp.json();
        
        if (result.success) {
            showToast(tMsg('msg_auto_classify_started', result.count), 'success');
        } else {
            showToast(tMsg('msg_auto_classify_start_failed') + ': ' + result.error, 'error');
            if (autoClassifyEventSource) {
                autoClassifyEventSource.close();
                autoClassifyEventSource = null;
            }
        }
    } catch (e) {
        showToast(tMsg('msg_auto_classify_start_failed') + ': ' + e.message, 'error');
    }
};

window.stopAutoClassify = async function() {
    const stopBtn = document.getElementById('stopAutoClassifyBtn');
    
    try {
        // 按钮反馈
        if (stopBtn) {
            stopBtn.disabled = true;
            stopBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> 停止中';
        }
        
        if (autoClassifyEventSource) {
            autoClassifyEventSource.close();
            autoClassifyEventSource = null;
        }
        
        const resp = await fetch('/api/auto_classify/stop', { method: 'POST' });
        const result = await resp.json();
        
        if (result.success) {
            showToast(tMsg('msg_auto_classify_stopped'), 'success');
            const progressText = document.getElementById('autoClassifyProgressText');
            if (progressText) progressText.textContent = tMsg('msg_auto_classify_stopped');
            
            // 隐藏进度卡片
            setTimeout(() => {
                const card = document.getElementById('autoClassifyProgressCard');
                if (card) card.style.display = 'none';
            }, 1000);
        } else {
            showToast(tMsg('msg_stop_failed') + ': ' + (result.error || ''), 'error');
        }
    } catch (e) {
        showToast(tMsg('msg_stop_failed') + ': ' + e.message, 'error');
    } finally {
        // 恢复按钮
        if (stopBtn) {
            stopBtn.disabled = false;
            stopBtn.innerHTML = '<i class="fas fa-stop"></i> 停止';
        }
    }
};

document.addEventListener('DOMContentLoaded', async () => {
    console.log('TD-light Portal Loaded');
    
    // 从配置加载默认数据库名
    try {
        const resp = await fetch('/api/config');
        const config = await resp.json();
        const dbName = config.database?.name || 'gaiadr2_lc';
        
        // 同步到导入页面的数据库名输入框
        const importDbInput = document.getElementById('importDbName');
        if (importDbInput && !importDbInput.value) {
            importDbInput.value = dbName;
        }
    } catch (e) {
        console.log('Failed to load config for db name');
    }
    
    // 切换到导入页面时刷新候选数量
    const importTab = document.getElementById('nav-import');
    if (importTab) {
        importTab.addEventListener('click', () => {
            setTimeout(() => window.onDbNameChange(), 100);
        });
    }
});

// ============================================================
// TDlight Incremental Training UI
// ============================================================

let trainEventSource = null;
let isTrainingRunning = false;

window.onTrainFileSelected = function(input) {
    const nameEl = document.getElementById('trainFileName');
    if (input.files && input.files.length > 0) {
        const file = input.files[0];
        const sizeMB = (file.size / 1024 / 1024).toFixed(2);
        nameEl.innerHTML = `<i class="fas fa-file-archive"></i> ${file.name} <span style="color:var(--text-secondary);">(${sizeMB} MB)</span>`;
        nameEl.style.color = 'var(--accent)';
    } else {
        nameEl.textContent = '';
    }
};

window.startTraining = async function() {
    const fileInput = document.getElementById('trainFileInput');
    const labelSelect = document.getElementById('trainLabelSelect');
    const doTrain = document.getElementById('trainDoTrain').checked;
    
    if (!fileInput.files || fileInput.files.length === 0) {
        showToast('请选择训练文件 / Please select a training file', 'error');
        return;
    }
    if (!labelSelect.value) {
        showToast('请选择类别标签或启用自动识别 / Please select a label or enable auto-detect', 'error');
        return;
    }
    
    const file = fileInput.files[0];
    const label = labelSelect.value === 'auto' ? 'auto' : labelSelect.value;
    const taskId = 'train_' + Date.now();
    
    // UI state
    isTrainingRunning = true;
    document.getElementById('trainStartBtn').style.display = 'none';
    document.getElementById('trainStopBtn').style.display = 'inline-block';
    document.getElementById('trainProgressPanel').style.display = 'block';
    document.getElementById('trainProgressBar').style.width = '1%';
    document.getElementById('trainProgressText').textContent = 'Uploading file...';
    document.getElementById('trainSpinner').className = 'fas fa-spinner fa-spin';
    
    try {
        // Step 1: Upload file
        const formData = new FormData();
        formData.append('file', file);
        
        const uploadResp = await fetch('http://localhost:5002/api/train/upload', {
            method: 'POST',
            body: formData
        });
        const uploadData = await uploadResp.json();
        if (!uploadData.success) {
            throw new Error(uploadData.error || 'Upload failed');
        }
        
        // Step 2: Start training
        const startResp = await fetch('http://localhost:5002/api/train/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                filepath: uploadData.path,
                label: label,
                train: doTrain,
                task_id: taskId
            })
        });
        const startData = await startResp.json();
        if (!startData.success) {
            throw new Error(startData.error || 'Failed to start training');
        }
        
        // Step 3: Listen to SSE progress
        if (trainEventSource) {
            trainEventSource.close();
        }
        
        trainEventSource = new EventSource('http://localhost:5002/api/train/stream?task_id=' + taskId);
        
        trainEventSource.onmessage = async (event) => {
            try {
                const status = JSON.parse(event.data);
                const bar = document.getElementById('trainProgressBar');
                const text = document.getElementById('trainProgressText');
                const stepEl = document.getElementById('trainProgressStep');
                
                const pct = status.percent || 0;
                if (bar) bar.style.width = pct + '%';
                if (text) text.textContent = status.message || '';
                if (stepEl) stepEl.textContent = status.step ? `[${status.step}]` : '';
                const pctEl = document.getElementById('trainProgressPercent');
                if (pctEl) pctEl.textContent = pct + '%';
                
                if (status.percent >= 100 || status.step === 'done' || status.step === 'error') {
                    trainEventSource.close();
                    trainEventSource = null;
                    isTrainingRunning = false;
                    document.getElementById('trainStartBtn').style.display = 'inline-block';
                    document.getElementById('trainStopBtn').style.display = 'none';
                    document.getElementById('trainSpinner').className = 'fas fa-check';
                    
                    if (status.step === 'error') {
                        showToast('训练失败 / Training failed: ' + status.message, 'error');
                    } else {
                        showToast('训练完成 / Training complete!', 'success');
                        window.loadTrainingSummary();
                        document.getElementById('trainOnnxPanel').style.display = 'block';
                    }
                }
            } catch (e) {
                console.error('Train SSE Error:', e);
            }
        };
        
        trainEventSource.onerror = (e) => {
            console.log('Train SSE connection closed');
            if (trainEventSource) {
                trainEventSource.close();
                trainEventSource = null;
            }
        };
        
    } catch (error) {
        isTrainingRunning = false;
        document.getElementById('trainStartBtn').style.display = 'inline-block';
        document.getElementById('trainStopBtn').style.display = 'none';
        document.getElementById('trainSpinner').className = 'fas fa-exclamation-triangle';
        showToast('Error: ' + error.message, 'error');
    }
};

window.exportOnnxModels = async function() {
    const btn = document.getElementById('trainOnnxBtn');
    const status = document.getElementById('trainOnnxStatus');
    btn.disabled = true;
    status.textContent = 'Exporting...';
    status.style.color = 'var(--text-secondary)';
    try {
        const resp = await fetch('http://localhost:5002/api/train/export_onnx', { method: 'POST' });
        const data = await resp.json();
        if (data.success) {
            status.textContent = 'Exported: ' + data.exported.join(', ');
            status.style.color = 'var(--success)';
            showToast('ONNX export completed', 'success');
        } else {
            status.textContent = data.error || 'Export failed';
            status.style.color = 'var(--danger)';
        }
    } catch (error) {
        status.textContent = 'Error: ' + error.message;
        status.style.color = 'var(--danger)';
    } finally {
        btn.disabled = false;
    }
};

window.stopTraining = async function() {
    try {
        await fetch('http://localhost:5002/api/train/stop', { method: 'POST' });
        showToast('已发送停止信号 / Stop signal sent', 'warning');
        if (trainEventSource) {
            trainEventSource.close();
            trainEventSource = null;
        }
        isTrainingRunning = false;
        document.getElementById('trainStartBtn').style.display = 'inline-block';
        document.getElementById('trainStopBtn').style.display = 'none';
        document.getElementById('trainSpinner').className = 'fas fa-stop';
    } catch (e) {
        showToast('停止失败 / Stop failed: ' + e.message, 'error');
    }
};

window.loadTrainingSummary = async function() {
    const container = document.getElementById('trainSummaryContent');
    container.innerHTML = '<p><i class="fas fa-spinner fa-spin"></i> 加载中 / Loading...</p>';
    
    try {
        const resp = await fetch('http://localhost:5002/api/train/summary');
        const data = await resp.json();
        if (!data.success) {
            container.innerHTML = '<p style="color: var(--danger);">加载失败 / Failed to load</p>';
            return;
        }
        
        const summary = data.summary;
        if (!summary || Object.keys(summary).length === 0) {
            container.innerHTML = '<p>暂无训练数据 / No training data yet</p>';
            return;
        }
        
        let html = '<table style="width:100%; border-collapse: collapse; font-size: 0.9rem;">';
        html += '<tr style="border-bottom: 1px solid var(--border);">';
        html += '<th style="text-align:left; padding: 8px;">子模型 / Sub-model</th>';
        html += '<th style="text-align:center; padding: 8px;">样本数 / Samples</th>';
        html += '<th style="text-align:left; padding: 8px;">类别分布 / Distribution</th>';
        html += '</tr>';
        
        for (const [name, info] of Object.entries(summary)) {
            const dist = info.class_distribution || {};
            const distStr = Object.entries(dist).map(([k,v]) => `${k}: ${v}`).join(', ');
            html += `<tr style="border-bottom: 1px solid var(--border);">`;
            html += `<td style="padding: 8px; color: var(--text);">${name}</td>`;
            html += `<td style="padding: 8px; text-align:center; color: var(--accent); font-weight: bold;">${info.total_samples}</td>`;
            html += `<td style="padding: 8px; color: var(--text-secondary); font-size: 0.85rem;">${distStr}</td>`;
            html += `</tr>`;
        }
        html += '</table>';
        container.innerHTML = html;
        
    } catch (e) {
        container.innerHTML = '<p style="color: var(--danger);">加载失败 / Failed to load: ' + e.message + '</p>';
    }
};

window.clearTrainingData = async function() {
    if (!confirm('确定要清空所有积累的训练数据吗？此操作不可恢复。\nAre you sure you want to clear all accumulated training data? This cannot be undone.')) {
        return;
    }
    try {
        const resp = await fetch('http://localhost:5002/api/train/clear', { method: 'POST' });
        const data = await resp.json();
        if (data.success) {
            showToast('训练数据已清空 / Training data cleared', 'success');
            window.loadTrainingSummary();
        } else {
            showToast('清空失败 / Clear failed: ' + (data.error || ''), 'error');
        }
    } catch (e) {
        showToast('清空失败 / Clear failed: ' + e.message, 'error');
    }
};
