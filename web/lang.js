// TD-light Internationalization (i18n)
// Language configuration for Chinese and English

console.log('[i18n] lang.js script loading...');

const i18n = {
    zh: {
        // Header
        title: 'TD-light 光变曲线分类系统',
        mode: 'Stepper Mode',
        nav_portal: '天体分类',
        nav_import: '数据导入',
        nav_settings: '系统设置',
        
        // Cone Search
        cone_search: '锥形搜索',
        center_ra: '中心 RA (度)',
        center_dec: '中心 DEC (度)',
        search_radius: '搜索半径 (度)',
        search: '搜索',
        append: '追加',
        
        // ID Search
        id_search: 'ID 检索',
        source_id_input: 'Source ID (支持多个，每行一个)',
        source_id_placeholder: '3602817205828729344\n3795050836667031936\n...',
        retrieve: '检索',
        
        // Rect Search
        rect_search: '矩形检索',
        ra_min: 'RA 最小值 (度)',
        ra_max: 'RA 最大值 (度)',
        dec_min: 'DEC 最小值 (度)',
        dec_max: 'DEC 最大值 (度)',
        
        // Object List
        objects_to_classify: '待分类天体',
        select_all: '全选',
        deselect_all: '取消全选',
        empty_list_hint: '使用上方搜索功能查找天体',
        start_classify: '开始分类',
        classifying: '分类中...',
        
        // Sky Map
        toggle_2d_3d: '切换 2D/3D',
        sky_map_hint: '搜索区域后显示天图',
        
        // Light Curve
        light_curve: '光变曲线',
        click_to_view: '点击天体查看光变曲线',
        time_axis: '时间',
        magnitude_axis: '星等',
        
        // Classification Progress
        classify_progress: '分类进度',
        step_extract: '提取数据',
        step_feature: '提取特征',
        step_predict: '分类预测',
        preparing: '准备中...',
        stop_classify: '停止分类',
        
        // Classification Results
        classify_results: '分类结果',
        coordinates: '坐标 (RA, DEC)',
        classification: '分类',
        confidence: '置信度',
        show_results_hint: '完成分类后显示结果',
        
        // Object Info
        object_info: '天体信息',
        source_id: 'Source ID',
        healpix_id: 'Healpix ID',
        object_class: '类别',
        data_points: '数据点数',
        
        // Data Import Page
        common_config: '公共配置',
        sync_config: '同步配置',
        coords_path_label: '坐标文件路径 (source_coordinates.csv)',
        database_name: '数据库名',
        healpix_nside: 'HEALPix NSIDE',
        
        // Database Management
        db_management: '数据库管理',
        db_management_hint: '删除现有数据库以释放 VGroups 资源（重新导入前需要先删除）',
        select_database: '选择数据库',
        click_refresh: '-- 点击刷新列表 --',
        delete: '删除',
        
        // Catalog Import
        catalog_import: '星表导入',
        catalog_import_hint: '从 CSV 导入星表元数据，自动根据 ra/dec 计算 HEALPix ID',
        catalog_path: '星表目录路径',
        start_import: '开始导入',
        
        // Lightcurve Import
        lightcurve_import: '光变曲线导入',
        lightcurve_import_hint: '从 CSV 导入光变曲线时序数据',
        lightcurve_path: '光变曲线目录路径',
        
        // Import Progress
        import_progress: '导入进度',
        stop: '停止',
        step_create_tables: '建表',
        step_insert: '插入',
        step_complete: '完成',
        
        // Auto Classification
        auto_classify: '自动分类',
        query: '查询',
        pending_classify: '待分类',
        auto_classify_hint: '检测新增或增长>20%的光变曲线',
        start: '开始',
        resume: '继续',
        auto_classify_progress: '自动分类进度',
        current_batch: '当前批次',
        batch_progress: '批次进度',
        updated: '已更新',
        
        // Usage Instructions
        usage_instructions: '使用说明',
        usage_catalog: '星表导入：每个 CSV 文件包含一个 HEALPix 区域的星表，文件名格式为',
        usage_lightcurve: '光变曲线导入：每个 CSV 文件包含一个天体的光变曲线，文件名格式为',
        usage_auto: '自动分类：导入时自动检测新出现或数据点增长超过20%的光变曲线，加入分类队列，每批5000条处理。',
        usage_note: '注意：导入是资源密集型操作，建议在服务器负载较低时进行。',
        
        // Settings Page
        db_config: '数据库配置',
        host_address: '主机地址',
        port: '端口',
        username: '用户名',
        
        import_config: '导入配置',
        threads: '线程数',
        vgroups: 'VGroups 数',
        threads_hint: '建议: 低配 8-16, 高配 32-64',
        vgroups_hint: '建议: 低配 16-32, 高配 64-128',
        
        classify_config: '分类配置',
        confidence_threshold: '置信度阈值',
        model_path: '模型路径',
        
        save_config: '保存配置',
        reload_config: '重新加载',
        apply_to_backend: '应用到后端',
        
        // Messages
        msg_searching: '正在搜索...',
        msg_found_objects: '找到 {0} 个天体',
        msg_appended_objects: '追加了 {0} 个天体',
        msg_all_in_list: '所有天体已在列表中',
        msg_no_objects_found: '该区域未找到天体',
        msg_search_failed: '搜索失败',
        msg_retrieving: '正在检索 {0} 个ID...',
        msg_not_found: '未找到任何天体',
        msg_retrieve_failed: '检索失败',
        msg_list_cleared: '已清空列表',
        msg_select_objects: '请先选择要分类的天体',
        msg_classify_success: '成功分类 {0} 个天体！',
        msg_classify_stopped: '分类已停止',
        msg_config_loaded: '配置加载成功',
        msg_config_load_failed: '加载配置失败',
        msg_config_saved: '配置已保存！重启服务后生效。',
        msg_config_save_failed: '保存失败',
        msg_config_synced: '配置已同步',
        msg_config_sync_failed: '同步配置失败',
        msg_config_applied: '配置已保存并应用到后端！',
        msg_apply_failed: '应用失败',
        msg_import_started: '导入任务已启动',
        msg_import_start_failed: '启动导入失败',
        msg_import_complete: '导入完成!',
        msg_import_stopped: '导入已停止',
        msg_stop_failed: '停止失败',
        msg_db_refreshed: '已刷新，共 {0} 个用户数据库',
        msg_no_user_db: '没有用户数据库',
        msg_refresh_failed: '刷新失败',
        msg_select_db_delete: '请先选择要删除的数据库',
        msg_confirm_delete: '确定要删除数据库 "{0}" 吗？\n\n此操作不可恢复！',
        msg_db_deleted: '数据库 {0} 已删除',
        msg_delete_failed: '删除失败',
        msg_detection_querying: '正在查询数据库...',
        msg_detection_failed: '检测失败',
        msg_queue_empty: '队列为空',
        msg_queue_info: '{0} 条，分 {1} 批',
        msg_click_query: '队列为空，点击查询检测',
        msg_auto_classify_started: '自动分类已启动，共 {0} 个天体',
        msg_auto_classify_start_failed: '启动失败',
        msg_auto_classify_complete: '自动分类完成！',
        msg_auto_classify_paused: '自动分类已暂停，可点击"继续"恢复',
        msg_auto_classify_error: '自动分类出错',
        msg_auto_classify_stopped: '自动分类已停止',
        msg_enter_valid_coords: '请输入有效的 RA, DEC 和半径',
        msg_enter_source_id: '请输入 Source ID',
        msg_enter_valid_id: '请输入有效的 Source ID',
        msg_enter_valid_range: '请输入有效的坐标范围',
        msg_min_less_max: '最小值必须小于最大值',
        msg_enter_catalog_path: '请输入星表目录路径',
        msg_enter_coords_path: '请输入坐标文件路径',
        msg_enter_lc_path: '请输入光变曲线目录路径',
        msg_no_lc_data: '未找到光变曲线数据',
        msg_get_lc_failed: '获取光变曲线失败',
        msg_list_empty: '列表为空',
        msg_no_lc_data_download: '没有光变曲线数据',
        msg_object_not_found: '未找到天体信息',
        msg_enter_db_name: '请填写数据库名',
        msg_auto_detect_hint: '检测到 {0} 个天体待分类',
        msg_auto_starting: '正在自动开始分类...',
        
        // Auto import classification toggle
        auto_classify_after_import: '导入后自动分类',
        auto_classify_after_import_hint: '导入完成后自动检测并分类新数据',
    },
    
    en: {
        // Header
        title: 'TD-light Light Curve Classification',
        mode: 'Stepper Mode',
        nav_portal: 'Classification',
        nav_import: 'Data Import',
        nav_settings: 'Settings',
        
        // Cone Search
        cone_search: 'Cone Search',
        center_ra: 'Center RA (deg)',
        center_dec: 'Center DEC (deg)',
        search_radius: 'Search Radius (deg)',
        search: 'Search',
        append: 'Append',
        
        // ID Search
        id_search: 'ID Search',
        source_id_input: 'Source ID (one per line)',
        source_id_placeholder: '3602817205828729344\n3795050836667031936\n...',
        retrieve: 'Retrieve',
        
        // Rect Search
        rect_search: 'Region Search',
        ra_min: 'RA Min (deg)',
        ra_max: 'RA Max (deg)',
        dec_min: 'DEC Min (deg)',
        dec_max: 'DEC Max (deg)',
        
        // Object List
        objects_to_classify: 'Objects to Classify',
        select_all: 'Select All',
        deselect_all: 'Deselect All',
        empty_list_hint: 'Use search above to find objects',
        start_classify: 'Start Classification',
        classifying: 'Classifying...',
        
        // Sky Map
        toggle_2d_3d: 'Toggle 2D/3D',
        sky_map_hint: 'Search to display sky map',
        
        // Light Curve
        light_curve: 'Light Curve',
        click_to_view: 'Click object to view light curve',
        time_axis: 'Time',
        magnitude_axis: 'Magnitude',
        
        // Classification Progress
        classify_progress: 'Classification Progress',
        step_extract: 'Extract Data',
        step_feature: 'Extract Features',
        step_predict: 'Classify',
        preparing: 'Preparing...',
        stop_classify: 'Stop',
        
        // Classification Results
        classify_results: 'Classification Results',
        coordinates: 'Coordinates (RA, DEC)',
        classification: 'Class',
        confidence: 'Confidence',
        show_results_hint: 'Results shown after classification',
        
        // Object Info
        object_info: 'Object Info',
        source_id: 'Source ID',
        healpix_id: 'Healpix ID',
        object_class: 'Class',
        data_points: 'Data Points',
        
        // Data Import Page
        common_config: 'Common Settings',
        sync_config: 'Sync Config',
        coords_path_label: 'Coordinates File Path (source_coordinates.csv)',
        database_name: 'Database Name',
        healpix_nside: 'HEALPix NSIDE',
        
        // Database Management
        db_management: 'Database Management',
        db_management_hint: 'Delete existing databases to free VGroups resources',
        select_database: 'Select Database',
        click_refresh: '-- Click to Refresh --',
        delete: 'Delete',
        
        // Catalog Import
        catalog_import: 'Catalog Import',
        catalog_import_hint: 'Import catalog metadata from CSV, auto-compute HEALPix ID from ra/dec',
        catalog_path: 'Catalog Directory Path',
        start_import: 'Start Import',
        
        // Lightcurve Import
        lightcurve_import: 'Light Curve Import',
        lightcurve_import_hint: 'Import light curve time-series data from CSV',
        lightcurve_path: 'Light Curve Directory Path',
        
        // Import Progress
        import_progress: 'Import Progress',
        stop: 'Stop',
        step_create_tables: 'Tables',
        step_insert: 'Insert',
        step_complete: 'Done',
        
        // Auto Classification
        auto_classify: 'Auto Classification',
        query: 'Query',
        pending_classify: 'Pending',
        auto_classify_hint: 'Detect new or >20% grown light curves',
        start: 'Start',
        resume: 'Resume',
        auto_classify_progress: 'Auto Classification Progress',
        current_batch: 'Current Batch',
        batch_progress: 'Batch Progress',
        updated: 'Updated',
        
        // Usage Instructions
        usage_instructions: 'Usage Guide',
        usage_catalog: 'Catalog Import: Each CSV contains one HEALPix region catalog, filename format:',
        usage_lightcurve: 'Light Curve Import: Each CSV contains one object\'s light curve, filename format:',
        usage_auto: 'Auto Classification: Automatically detects new or >20% grown light curves after import, processes in batches of 5000.',
        usage_note: 'Note: Import is resource-intensive. Run during low server load.',
        
        // Settings Page
        db_config: 'Database Configuration',
        host_address: 'Host Address',
        port: 'Port',
        username: 'Username',
        
        import_config: 'Import Configuration',
        threads: 'Threads',
        vgroups: 'VGroups',
        threads_hint: 'Suggested: Low 8-16, High 32-64',
        vgroups_hint: 'Suggested: Low 16-32, High 64-128',
        
        classify_config: 'Classification Configuration',
        confidence_threshold: 'Confidence Threshold',
        model_path: 'Model Path',
        
        save_config: 'Save Config',
        reload_config: 'Reload',
        apply_to_backend: 'Apply to Backend',
        
        // Messages
        msg_searching: 'Searching...',
        msg_found_objects: 'Found {0} objects',
        msg_appended_objects: 'Appended {0} objects',
        msg_all_in_list: 'All objects already in list',
        msg_no_objects_found: 'No objects found in this region',
        msg_search_failed: 'Search failed',
        msg_retrieving: 'Retrieving {0} IDs...',
        msg_not_found: 'No objects found',
        msg_retrieve_failed: 'Retrieve failed',
        msg_list_cleared: 'List cleared',
        msg_select_objects: 'Please select objects to classify',
        msg_classify_success: 'Successfully classified {0} objects!',
        msg_classify_stopped: 'Classification stopped',
        msg_config_loaded: 'Config loaded',
        msg_config_load_failed: 'Failed to load config',
        msg_config_saved: 'Config saved! Restart required.',
        msg_config_save_failed: 'Save failed',
        msg_config_synced: 'Config synced',
        msg_config_sync_failed: 'Sync failed',
        msg_config_applied: 'Config saved and applied!',
        msg_apply_failed: 'Apply failed',
        msg_import_started: 'Import task started',
        msg_import_start_failed: 'Failed to start import',
        msg_import_complete: 'Import complete!',
        msg_import_stopped: 'Import stopped',
        msg_stop_failed: 'Stop failed',
        msg_db_refreshed: 'Refreshed: {0} user databases',
        msg_no_user_db: 'No user databases',
        msg_refresh_failed: 'Refresh failed',
        msg_select_db_delete: 'Please select a database to delete',
        msg_confirm_delete: 'Delete database "{0}"?\n\nThis cannot be undone!',
        msg_db_deleted: 'Database {0} deleted',
        msg_delete_failed: 'Delete failed',
        msg_detection_querying: 'Querying database...',
        msg_detection_failed: 'Detection failed',
        msg_queue_empty: 'Queue empty',
        msg_queue_info: '{0} items, {1} batches',
        msg_click_query: 'Queue empty, click Query to detect',
        msg_auto_classify_started: 'Auto-classification started: {0} objects',
        msg_auto_classify_start_failed: 'Failed to start',
        msg_auto_classify_complete: 'Auto-classification complete!',
        msg_auto_classify_paused: 'Paused. Click Resume to continue.',
        msg_auto_classify_error: 'Auto-classification error',
        msg_auto_classify_stopped: 'Auto-classification stopped',
        msg_enter_valid_coords: 'Enter valid RA, DEC and radius',
        msg_enter_source_id: 'Enter Source ID',
        msg_enter_valid_id: 'Enter valid Source ID',
        msg_enter_valid_range: 'Enter valid coordinate range',
        msg_min_less_max: 'Min must be less than Max',
        msg_enter_catalog_path: 'Enter catalog directory path',
        msg_enter_coords_path: 'Enter coordinates file path',
        msg_enter_lc_path: 'Enter light curve directory path',
        msg_no_lc_data: 'No light curve data found',
        msg_get_lc_failed: 'Failed to get light curve',
        msg_list_empty: 'List is empty',
        msg_no_lc_data_download: 'No light curve data',
        msg_object_not_found: 'Object not found',
        msg_enter_db_name: 'Enter database name',
        msg_auto_detect_hint: 'Detected {0} objects for classification',
        msg_auto_starting: 'Auto-starting classification...',
        
        // Auto import classification toggle
        auto_classify_after_import: 'Auto-classify after import',
        auto_classify_after_import_hint: 'Automatically detect and classify new data after import',
    }
};

// Current language
let currentLang = localStorage.getItem('tdlight_lang') || 'zh';

// Get translation
function t(key, ...args) {
    if (!i18n[currentLang]) {
        console.warn('[i18n] Invalid language:', currentLang, 'falling back to zh');
        currentLang = 'zh';
    }
    
    let text = i18n[currentLang][key];
    if (!text) {
        // Fallback to Chinese
        text = i18n['zh'][key];
        if (!text) {
            console.warn('[i18n] Translation not found for key:', key, 'in language:', currentLang);
            text = key; // Return key as fallback
        }
    }
    
    // Replace {0}, {1}, etc. with arguments
    args.forEach((arg, i) => {
        text = text.replace(`{${i}}`, arg);
    });
    return text;
}

// Set language
function setLanguage(lang) {
    if (i18n[lang]) {
        currentLang = lang;
        localStorage.setItem('tdlight_lang', lang);
        updatePageLanguage();
    }
}

// Update all page elements with data-i18n attribute
function updatePageLanguage() {
    console.log('[i18n] Updating page language to:', currentLang);
    let updatedCount = 0;
    
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        const translation = t(key);
        
        if (el.tagName === 'INPUT' && (el.type === 'text' || el.type === 'number' || el.type === 'email')) {
            el.placeholder = translation;
            updatedCount++;
        } else if (el.tagName === 'TEXTAREA') {
            el.placeholder = translation;
            updatedCount++;
        } else if (el.tagName === 'OPTION') {
            el.textContent = translation;
            updatedCount++;
        } else {
            // For other elements, check if they have child elements
            const hasChildElements = Array.from(el.children).length > 0;
            
            if (hasChildElements) {
                // Element has children (like <span data-i18n="...">Text</span>)
                // Update only the direct text content, preserving child elements
                const textNodes = Array.from(el.childNodes).filter(node => node.nodeType === Node.TEXT_NODE);
                if (textNodes.length > 0) {
                    // Update first text node
                    textNodes[0].textContent = translation;
                    // Remove other text nodes
                    textNodes.slice(1).forEach(node => node.remove());
                } else {
                    // No text node, prepend one (before children)
                    el.insertBefore(document.createTextNode(translation), el.firstChild);
                }
            } else {
                // No children, safe to update textContent
                el.textContent = translation;
            }
            updatedCount++;
        }
    });
    
    // Update title attributes
    document.querySelectorAll('[data-i18n-title]').forEach(el => {
        const key = el.getAttribute('data-i18n-title');
        el.title = t(key);
        updatedCount++;
    });
    
    // Update placeholders
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        const key = el.getAttribute('data-i18n-placeholder');
        el.placeholder = t(key);
        updatedCount++;
    });
    
    // Update page title
    document.title = t('title');
    
    // Update language switcher button text
    const langText = document.getElementById('langText');
    if (langText) {
        langText.textContent = currentLang === 'zh' ? 'EN' : '中文';
    }
    
    // Update html lang attribute
    document.documentElement.lang = currentLang === 'zh' ? 'zh-CN' : 'en';
    
    console.log('[i18n] Page language updated. Elements updated:', updatedCount);
}

// Toggle language
function toggleLanguage() {
    const oldLang = currentLang;
    const newLang = currentLang === 'zh' ? 'en' : 'zh';
    console.log('[i18n] Toggle called. Current:', oldLang, '-> New:', newLang);
    
    if (i18n[newLang]) {
        currentLang = newLang;
        localStorage.setItem('tdlight_lang', newLang);
        console.log('[i18n] Language set to:', currentLang);
        updatePageLanguage();
        console.log('[i18n] Page updated');
    } else {
        console.error('[i18n] Invalid language:', newLang);
    }
}

// Export for use in app.js and inline handlers
// Export immediately - this runs synchronously when script loads
console.log('[i18n] About to export functions...');
console.log('[i18n] toggleLanguage defined:', typeof toggleLanguage);
console.log('[i18n] t defined:', typeof t);
console.log('[i18n] window available:', typeof window !== 'undefined');

if (typeof window !== 'undefined') {
    try {
        window.i18n = i18n;
        window.t = t;
        window.setLanguage = setLanguage;
        window.currentLang = function() { return currentLang; };
        // Directly export the function (not a wrapper)
        window.toggleLanguage = toggleLanguage;
        window.updatePageLanguage = updatePageLanguage;
        console.log('[i18n] Functions exported to window:', {
            toggleLanguage: typeof window.toggleLanguage,
            t: typeof window.t,
            updatePageLanguage: typeof window.updatePageLanguage,
            i18n: typeof window.i18n
        });
    } catch (e) {
        console.error('[i18n] Error exporting functions:', e);
    }
} else {
    console.error('[i18n] window object not available!');
}

// Auto-initialize when DOM is ready
function initLanguage() {
    console.log('[i18n] Initializing language system...');
    console.log('[i18n] Current language from storage:', localStorage.getItem('tdlight_lang'));
    console.log('[i18n] Default language:', currentLang);
    console.log('[i18n] window.toggleLanguage available:', typeof window.toggleLanguage);
    
    // Update page language
    updatePageLanguage();
    
    // Bind button click event - this is the primary method
    const langBtn = document.getElementById('langSwitcher');
    if (langBtn) {
        console.log('[i18n] Language button found, binding click handler');
        
        // Remove any existing handlers
        langBtn.onclick = null;
        
        // Add click event listener
        langBtn.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            console.log('[i18n] Button clicked via event listener');
            
            if (typeof window.toggleLanguage === 'function') {
                console.log('[i18n] Calling window.toggleLanguage()');
                window.toggleLanguage();
            } else {
                console.error('[i18n] window.toggleLanguage is not a function! Type:', typeof window.toggleLanguage);
                // Fallback: direct toggle
                const oldLang = currentLang;
                const newLang = currentLang === 'zh' ? 'en' : 'zh';
                console.log('[i18n] Fallback: Direct toggle', oldLang, '->', newLang);
                if (i18n[newLang]) {
                    currentLang = newLang;
                    localStorage.setItem('tdlight_lang', newLang);
                    updatePageLanguage();
                }
            }
            return false;
        });
        
        console.log('[i18n] Language button event listener attached successfully');
    } else {
        console.warn('[i18n] Language button not found!');
    }
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initLanguage);
} else {
    // DOM already loaded, initialize immediately
    initLanguage();
}

