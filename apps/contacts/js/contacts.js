// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 연락처 앱 — 연락처 CRUD 및 검색
// 수행범위: 연락처 목록, 상세보기, 편집/추가 폼, contacts 서비스 연동
// 의존방향: contacts 서비스 (postMessage IPC)
// SOLID: SRP — 연락처 관리 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ── DOM References ── */
  var listView = document.getElementById('list-view');
  var detailView = document.getElementById('detail-view');
  var formView = document.getElementById('form-view');
  var contactList = document.getElementById('contact-list');
  var searchInput = document.getElementById('search-input');

  var detailAvatar = document.getElementById('detail-avatar');
  var detailName = document.getElementById('detail-name');
  var detailPhone = document.getElementById('detail-phone');
  var detailEmail = document.getElementById('detail-email');
  var detailPhoneText = document.getElementById('detail-phone-text');
  var detailEmailText = document.getElementById('detail-email-text');
  var deleteModal = document.getElementById('delete-modal');
  var modalCancel = document.getElementById('modal-cancel');
  var modalDelete = document.getElementById('modal-delete');

  var inputName = document.getElementById('input-name');
  var inputPhone = document.getElementById('input-phone');
  var inputEmail = document.getElementById('input-email');
  var formTitle = document.getElementById('form-title');

  /* ── State ── */
  var contacts = [];
  var currentContact = null;
  var editingId = null;
  var currentView = 'list'; // 'list' | 'detail' | 'form'

  /* ── Avatar Color Palette ── */
  var AVATAR_GRADIENTS = [
    'linear-gradient(135deg, #667eea, #764ba2)',
    'linear-gradient(135deg, #f093fb, #f5576c)',
    'linear-gradient(135deg, #4facfe, #00f2fe)',
    'linear-gradient(135deg, #43e97b, #38f9d7)',
    'linear-gradient(135deg, #fa709a, #fee140)',
    'linear-gradient(135deg, #a18cd1, #fbc2eb)',
    'linear-gradient(135deg, #fccb90, #d57eeb)',
    'linear-gradient(135deg, #e0c3fc, #8ec5fc)',
    'linear-gradient(135deg, #f5576c, #ff6a00)',
    'linear-gradient(135deg, #06b6d4, #3b82f6)'
  ];

  /* ── Utility ── */
  function escapeHtml(str) {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function getAvatarClass(name) {
    if (!name) return 'avatar-0';
    var hash = 0;
    for (var i = 0; i < name.length; i++) hash += name.charCodeAt(i);
    return 'avatar-' + (hash % 10);
  }

  function getAvatarGradient(name) {
    if (!name) return AVATAR_GRADIENTS[0];
    var hash = 0;
    for (var i = 0; i < name.length; i++) {
      hash = ((hash << 5) - hash) + name.charCodeAt(i);
      hash = hash & hash;
    }
    return AVATAR_GRADIENTS[Math.abs(hash) % AVATAR_GRADIENTS.length];
  }

  function getInitial(name) {
    if (!name) return '?';
    return name.charAt(0).toUpperCase();
  }

  function getSortKey(name) {
    if (!name) return '#';
    var ch = name.charAt(0).toUpperCase();
    if (ch >= 'A' && ch <= 'Z') return ch;
    return '#';
  }

  function t(key) {
    if (typeof zylI18n !== 'undefined' && typeof zylI18n.t === 'function') {
      return zylI18n.t(key);
    }
    return key;
  }

  /* ── IPC ── */
  function requestService(service, method, params) {
    var msg = {
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    };
    ZylBridge.sendToSystem(msg);
  }

  function loadContacts() {
    requestService('contacts', 'getAll');
  }

  /* ── View Management ── */
  function showView(name) {
    currentView = name;
    if (listView) listView.classList.toggle('hidden', name !== 'list');
    if (detailView) detailView.classList.toggle('hidden', name !== 'detail');
    if (formView) formView.classList.toggle('hidden', name !== 'form');
  }

  /* ── Render Contact List ── */
  function renderContacts(data) {
    if (!contactList) return;
    contactList.innerHTML = '';

    var query = searchInput ? searchInput.value.trim().toLowerCase() : '';
    var filtered = (data || []).filter(function (c) {
      if (!query) return true;
      var name = (c.name || '').toLowerCase();
      var phone = (c.phone || '').toLowerCase();
      return name.indexOf(query) !== -1 || phone.indexOf(query) !== -1;
    });

    if (filtered.length === 0) {
      contactList.innerHTML = '<div class="empty-state">' + escapeHtml(t('contacts.no_contacts')) + '</div>';
      return;
    }

    /* Sort alphabetically */
    filtered.sort(function (a, b) {
      var na = (a.name || '').toLowerCase();
      var nb = (b.name || '').toLowerCase();
      if (na < nb) return -1;
      if (na > nb) return 1;
      return 0;
    });

    /* Group by initial letter */
    var currentSection = '';
    var frag = document.createDocumentFragment();

    for (var i = 0; i < filtered.length; i++) {
      var c = filtered[i];
      var section = getSortKey(c.name);

      if (section !== currentSection) {
        currentSection = section;
        var header = document.createElement('div');
        header.className = 'section-header';
        header.textContent = currentSection;
        frag.appendChild(header);
      }

      var row = document.createElement('div');
      row.className = 'contact-row';
      row.setAttribute('data-id', c.id || '');

      var avatar = document.createElement('div');
      avatar.className = 'avatar ' + getAvatarClass(c.name);
      avatar.textContent = getInitial(c.name);

      var info = document.createElement('div');
      info.className = 'contact-info';

      var nameEl = document.createElement('div');
      nameEl.className = 'name';
      nameEl.textContent = c.name || '';

      var phoneEl = document.createElement('div');
      phoneEl.className = 'phone';
      phoneEl.textContent = c.phone || '';

      info.appendChild(nameEl);
      info.appendChild(phoneEl);
      row.appendChild(avatar);
      row.appendChild(info);

      (function (contact) {
        row.addEventListener('click', function () {
          showDetail(contact);
        });
      })(c);

      frag.appendChild(row);
    }

    contactList.appendChild(frag);
  }

  /* ── Detail View ── */
  function showDetail(contact) {
    currentContact = contact;
    if (detailAvatar) {
      detailAvatar.className = getAvatarClass(contact.name);
      detailAvatar.textContent = getInitial(contact.name);
    }
    if (detailName) detailName.textContent = contact.name || '';
    if (detailPhone) detailPhone.textContent = contact.phone || '';
    if (detailEmail) detailEmail.textContent = contact.email || '';
    /* Update new detail text elements */
    if (detailPhoneText) detailPhoneText.textContent = contact.phone || '';
    if (detailEmailText) detailEmailText.textContent = contact.email || '';
    showView('detail');
  }

  /* ── Form View ── */
  function showForm(contact) {
    editingId = contact ? (contact.id || null) : null;
    if (formTitle) {
      formTitle.textContent = contact ? t('contacts.edit') : t('contacts.add');
    }
    if (inputName) inputName.value = contact ? (contact.name || '') : '';
    if (inputPhone) inputPhone.value = contact ? (contact.phone || '') : '';
    if (inputEmail) inputEmail.value = contact ? (contact.email || '') : '';
    showView('form');
  }

  function saveContact() {
    var name = inputName ? inputName.value.trim() : '';
    var phone = inputPhone ? inputPhone.value.trim() : '';
    var email = inputEmail ? inputEmail.value.trim() : '';
    if (!name) return;

    var data = { name: name, phone: phone, email: email };

    if (editingId) {
      data.id = editingId;
      requestService('contacts', 'update', data);
    } else {
      requestService('contacts', 'create', data);
    }

    editingId = null;
    showView('list');
    loadContacts();
  }

  function deleteContact() {
    if (!currentContact || !currentContact.id) return;
    /* Show custom delete confirmation modal */
    if (deleteModal) {
      deleteModal.classList.remove('hidden');
    }
  }

  /* Modal event handlers */
  if (modalCancel) {
    modalCancel.addEventListener('click', function () {
      if (deleteModal) deleteModal.classList.add('hidden');
    });
  }
  if (modalDelete) {
    modalDelete.addEventListener('click', function () {
      if (deleteModal) deleteModal.classList.add('hidden');
      if (!currentContact || !currentContact.id) return;
      requestService('contacts', 'delete', { id: currentContact.id });
      currentContact = null;
      showView('list');
      loadContacts();
    });
  }

  /* ── Message Handler ── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        if (currentView === 'form') {
          showView(editingId ? 'detail' : 'list');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else if (currentView === 'detail') {
          currentContact = null;
          showView('list');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      /* Service responses */
      if (msg.type === 'service.response' && msg.service === 'contacts') {
        if (msg.method === 'getAll' && msg.data) {
          contacts = msg.data;
          renderContacts(contacts);
        }
        if (msg.method === 'create' || msg.method === 'update' || msg.method === 'delete') {
          loadContacts();
        }
      }
    } catch (err) { /* silent */ }
  });

  /* ── Event Bindings ── */
  var btnAdd = document.getElementById('btn-add');
  if (btnAdd) btnAdd.addEventListener('click', function () { showForm(null); });

  var detailBack = document.getElementById('detail-back');
  if (detailBack) detailBack.addEventListener('click', function () {
    currentContact = null;
    showView('list');
  });

  var formBack = document.getElementById('form-back');
  if (formBack) formBack.addEventListener('click', function () {
    showView(editingId ? 'detail' : 'list');
    editingId = null;
  });

  var btnSave = document.getElementById('btn-save');
  if (btnSave) btnSave.addEventListener('click', saveContact);

  var btnCancel = document.getElementById('btn-cancel');
  if (btnCancel) btnCancel.addEventListener('click', function () {
    showView(editingId ? 'detail' : 'list');
    editingId = null;
  });

  var btnEdit = document.getElementById('btn-edit');
  if (btnEdit) btnEdit.addEventListener('click', function () {
    if (currentContact) showForm(currentContact);
  });

  var btnDelete = document.getElementById('btn-delete');
  if (btnDelete) btnDelete.addEventListener('click', deleteContact);

  var btnCall = document.getElementById('btn-call');
  if (btnCall) btnCall.addEventListener('click', function () {
    if (!currentContact) return;
    ZylBridge.sendToSystem({
      type: 'app.launch',
      appId: 'com.zylos.phone',
      params: { action: 'call', number: currentContact.phone || '' }
    });
  });

  var btnMessage = document.getElementById('btn-message');
  if (btnMessage) btnMessage.addEventListener('click', function () {
    if (!currentContact) return;
    ZylBridge.sendToSystem({
      type: 'app.launch',
      appId: 'com.zylos.messages',
      params: { action: 'compose', number: currentContact.phone || '' }
    });
  });

  /* Search input */
  if (searchInput) {
    searchInput.addEventListener('input', function () {
      renderContacts(contacts);
    });
  }

  /* ── Init ── */
  loadContacts();
})();
