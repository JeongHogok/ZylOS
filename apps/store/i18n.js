// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: App Store translation data
// Scope: store.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Store 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'store.title': '앱스토어',
    'store.search': '앱 검색...',
    'store.search_placeholder': '앱 검색...',
    'store.install': '설치',
    'store.uninstall': '제거',
    'store.installed': '설치됨',
    'store.system_app': '시스템',
    'store.system': '시스템',
    'store.all': '전체',
    'store.tab_all': '전체',
    'store.tab_installed': '설치됨',
    'store.empty': '앱을 찾을 수 없습니다',
    'store.loading': '로딩 중...',
    'store.available': '설치 가능',
    'store.categories': '카테고리',
    'store.details': '상세 정보',
    'store.back': '뒤로',
    'store.installing': '설치 중...',
    'store.uninstalling': '제거 중...',
    'store.update': '업데이트',
    'store.updating': '업데이트 중...',
    'store.version': '버전',
    'store.category': '카테고리',
    'store.size': '크기',
    'store.description': '설명',
    'store.no_description': '설명이 없습니다'
  });

  zylI18n.addTranslations('en', {
    'store.title': 'App Store',
    'store.search': 'Search apps...',
    'store.search_placeholder': 'Search apps...',
    'store.install': 'Install',
    'store.uninstall': 'Uninstall',
    'store.installed': 'Installed',
    'store.system_app': 'System',
    'store.system': 'System',
    'store.all': 'All',
    'store.tab_all': 'All',
    'store.tab_installed': 'Installed',
    'store.empty': 'No apps found',
    'store.loading': 'Loading...',
    'store.available': 'Available',
    'store.categories': 'Categories',
    'store.details': 'Details',
    'store.back': 'Back',
    'store.installing': 'Installing...',
    'store.uninstalling': 'Uninstalling...',
    'store.update': 'Update',
    'store.updating': 'Updating...',
    'store.version': 'Version',
    'store.category': 'Category',
    'store.size': 'Size',
    'store.description': 'Description',
    'store.no_description': 'No description available'
  });

  zylI18n.addTranslations('ja', {
    'store.title': 'アプリストア',
    'store.search': 'アプリを検索...',
    'store.search_placeholder': 'アプリを検索...',
    'store.install': 'インストール',
    'store.uninstall': 'アンインストール',
    'store.installed': 'インストール済み',
    'store.system_app': 'システム',
    'store.system': 'システム',
    'store.all': 'すべて',
    'store.tab_all': 'すべて',
    'store.tab_installed': 'インストール済み',
    'store.empty': 'アプリが見つかりません',
    'store.loading': '読み込み中...',
    'store.available': '入手可能',
    'store.categories': 'カテゴリ',
    'store.details': '詳細',
    'store.back': '戻る',
    'store.installing': 'インストール中...',
    'store.uninstalling': 'アンインストール中...',
    'store.update': 'アップデート',
    'store.updating': 'アップデート中...',
    'store.version': 'バージョン',
    'store.category': 'カテゴリ',
    'store.size': 'サイズ',
    'store.description': '説明',
    'store.no_description': '説明はありません'
  });

  zylI18n.addTranslations('zh', {
    'store.title': '应用商店',
    'store.search': '搜索应用...',
    'store.search_placeholder': '搜索应用...',
    'store.install': '安装',
    'store.uninstall': '卸载',
    'store.installed': '已安装',
    'store.system_app': '系统',
    'store.system': '系统',
    'store.all': '全部',
    'store.tab_all': '全部',
    'store.tab_installed': '已安装',
    'store.empty': '未找到应用',
    'store.loading': '加载中...',
    'store.available': '可安装',
    'store.categories': '分类',
    'store.details': '详情',
    'store.back': '返回',
    'store.installing': '安装中...',
    'store.uninstalling': '卸载中...',
    'store.update': '更新',
    'store.updating': '更新中...',
    'store.version': '版本',
    'store.category': '分类',
    'store.size': '大小',
    'store.description': '描述',
    'store.no_description': '暂无描述'
  });

  zylI18n.addTranslations('es', {
    'store.title': 'App Store',
    'store.search': 'Buscar aplicaciones...',
    'store.search_placeholder': 'Buscar apps...',
    'store.install': 'Instalar',
    'store.uninstall': 'Desinstalar',
    'store.installed': 'Instalada',
    'store.system_app': 'Sistema',
    'store.system': 'Sistema',
    'store.all': 'Todas',
    'store.tab_all': 'Todas',
    'store.tab_installed': 'Instaladas',
    'store.empty': 'No se encontraron apps',
    'store.loading': 'Cargando...',
    'store.available': 'Disponibles',
    'store.categories': 'Categorías',
    'store.details': 'Detalles',
    'store.back': 'Atrás',
    'store.installing': 'Instalando...',
    'store.uninstalling': 'Desinstalando...',
    'store.update': 'Actualizar',
    'store.updating': 'Actualizando...',
    'store.version': 'Versión',
    'store.category': 'Categoría',
    'store.size': 'Tamaño',
    'store.description': 'Descripción',
    'store.no_description': 'Sin descripción disponible'
  });
})();
