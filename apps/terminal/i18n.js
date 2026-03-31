// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Terminal app translation data
// Scope: terminal.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Terminal 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'terminal.title': '터미널',
    'terminal.new_tab': '새 탭',
    'terminal.close_tab': '탭 닫기',
    'terminal.paste': '붙여넣기',
    'terminal.theme': '테마',
    'terminal.theme_green': '그린',
    'terminal.theme_amber': '앰버',
    'terminal.theme_blue': '블루',
    'terminal.tab_name': 'Term {n}'
  });

  zylI18n.addTranslations('en', {
    'terminal.title': 'Terminal',
    'terminal.new_tab': 'New Tab',
    'terminal.close_tab': 'Close Tab',
    'terminal.paste': 'Paste',
    'terminal.theme': 'Theme',
    'terminal.theme_green': 'Green',
    'terminal.theme_amber': 'Amber',
    'terminal.theme_blue': 'Blue',
    'terminal.tab_name': 'Term {n}'
  });

  zylI18n.addTranslations('ja', {
    'terminal.title': 'ターミナル',
    'terminal.new_tab': '新しいタブ',
    'terminal.close_tab': 'タブを閉じる',
    'terminal.paste': '貼り付け',
    'terminal.theme': 'テーマ',
    'terminal.theme_green': 'グリーン',
    'terminal.theme_amber': 'アンバー',
    'terminal.theme_blue': 'ブルー',
    'terminal.tab_name': 'Term {n}'
  });

  zylI18n.addTranslations('zh', {
    'terminal.title': '终端',
    'terminal.new_tab': '新标签页',
    'terminal.close_tab': '关闭标签页',
    'terminal.paste': '粘贴',
    'terminal.theme': '主题',
    'terminal.theme_green': '绿色',
    'terminal.theme_amber': '琥珀色',
    'terminal.theme_blue': '蓝色',
    'terminal.tab_name': 'Term {n}'
  });

  zylI18n.addTranslations('es', {
    'terminal.title': 'Terminal',
    'terminal.new_tab': 'Nueva pestaña',
    'terminal.close_tab': 'Cerrar pestaña',
    'terminal.paste': 'Pegar',
    'terminal.theme': 'Tema',
    'terminal.theme_green': 'Verde',
    'terminal.theme_amber': 'Ámbar',
    'terminal.theme_blue': 'Azul',
    'terminal.tab_name': 'Term {n}'
  });
})();
