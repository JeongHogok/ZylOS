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

  /* ── IPC (fire-and-forget for init/read) ── */
  function requestServiceFire(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  function loadContacts() {
    /* 권한 판정은 OS 몫이다: 거부되면 응답이 오지 않고 목록은 빈 상태로 남는다 */
    requestServiceFire('contacts', 'getAll');
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
      row.setAttribute('role', 'listitem');
      row.setAttribute('aria-label', c.name || '');
      row.setAttribute('tabindex', '0');
      (function (r) {
        r.addEventListener('keydown', function (e) {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            r.click();
          }
        });
      })(row);

      var avatar = document.createElement('div');
      /* FIX: getAvatarClass returns only the gradient class (e.g. 'avatar-3');
       *       preserve 'avatar' base class by concatenating instead of replacing. */
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
      /* FIX: Preserve 'avatar' base class in detail view as well */
      detailAvatar.className = 'avatar ' + getAvatarClass(contact.name);
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

    var method = editingId ? 'update' : 'create';
    if (editingId) { data.id = editingId; }

    ZylBridge.requestService('contacts', method, data).then(function () {
      if (typeof ZylToast !== 'undefined') ZylToast.success(t('contacts.save_success'));
      editingId = null;
      showView('list');
      loadContacts();
    }).catch(function () {
      if (typeof ZylToast !== 'undefined') ZylToast.error(t('contacts.save_error'));
    });
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
      var deleteId = currentContact.id;
      ZylBridge.requestService('contacts', 'delete', { id: deleteId }).then(function () {
        if (typeof ZylToast !== 'undefined') ZylToast.success(t('contacts.delete_success'));
        currentContact = null;
        showView('list');
        loadContacts();
      }).catch(function () {
        if (typeof ZylToast !== 'undefined') ZylToast.error(t('contacts.delete_error'));
      });
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
    /* Whichever app dials (rule 9): the OS decides */
    ZylIntent.startActivity({
      action: ZylIntent.ACTION.DIAL,
      data: currentContact.phone || '',
      mimeType: 'tel/*'
    });
  });

  var btnMessage = document.getElementById('btn-message');
  if (btnMessage) btnMessage.addEventListener('click', function () {
    if (!currentContact) return;
    ZylIntent.startActivity({
      action: ZylIntent.ACTION.SEND,
      mimeType: 'text/plain',
      extras: { number: currentContact.phone || '' }
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
