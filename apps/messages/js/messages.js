// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 메시지 앱 — 대화목록(검색/삭제), 채팅(보낸 결과 표시), 새 메시지
// 수행범위: 스레드 목록, 메시지 송수신, 연락처 선택·이름 표시, 시간 포맷, 검색,
//          삭제, 읽음 처리. 받은 메시지는 messaging 서비스의 MessageReceived 이벤트
// 의존방향: shared/bridge.js → postMessage IPC (messaging, contacts)
// SOLID: SRP — 메시지 관련 UI만 담당, OCP — 스레드/메시지 확장 가능
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════════
     IPC — postMessage service calls
     ═══════════════════════════════════════════════════════════ */

  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  var serviceCallbacks = {};

  function onServiceResponse(service, method, cb) {
    serviceCallbacks[service + '.' + method] = cb;
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      if (!e.data) return;
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        handleBack();
        return;
      }

      /* Service responses */
      if (msg.type === 'service.response' && msg.service && msg.method) {
        var key = msg.service + '.' + msg.method;
        if (serviceCallbacks[key]) {
          serviceCallbacks[key](msg.data, msg.error);
        }
        return;
      }

    } catch (err) { /* ignore parse errors */ }
  });

  /* ═══════════════════════════════════════════════════════════
     DOM References
     ═══════════════════════════════════════════════════════════ */

  var viewThreads = document.getElementById('view-threads');
  var viewChat = document.getElementById('view-chat');
  var viewNew = document.getElementById('view-new');

  /* Threads */
  var threadList = document.getElementById('thread-list');
  var threadsEmpty = document.getElementById('threads-empty');
  var btnNewMessage = document.getElementById('btn-new-message');
  var threadSearchInput = document.getElementById('thread-search-input');

  /* Chat */
  var btnChatBack = document.getElementById('btn-chat-back');
  var chatContactName = document.getElementById('chat-contact-name');
  var chatMessages = document.getElementById('chat-messages');
  var chatInput = document.getElementById('chat-input');
  var btnSend = document.getElementById('btn-send');

  /* New Message */
  var btnNewBack = document.getElementById('btn-new-back');
  var newRecipientInput = document.getElementById('new-recipient-input');
  var newContactList = document.getElementById('new-contact-list');

  /* Delete dialog */
  var deleteDialog = document.getElementById('delete-dialog');
  var btnDeleteCancel = document.getElementById('btn-delete-cancel');
  var btnDeleteConfirm = document.getElementById('btn-delete-confirm');

  /* ═══════════════════════════════════════════════════════════
     State
     ═══════════════════════════════════════════════════════════ */

  var currentView = 'threads';  /* threads | chat | new */
  var currentThreadId = null;
  var currentThreadNumber = '';
  var currentThreadName = '';
  var allThreads = [];  /* cached for search filtering */
  var allContacts = []; /* names for numbers; the recipient picker */
  var deleteTargetThreadId = null;

  /* ═══════════════════════════════════════════════════════════
     View Switching
     ═══════════════════════════════════════════════════════════ */

  function showView(name) {
    currentView = name;
    viewThreads.classList.toggle('active', name === 'threads');
    viewThreads.classList.toggle('hidden', name !== 'threads');
    viewChat.classList.toggle('active', name === 'chat');
    viewChat.classList.toggle('hidden', name !== 'chat');
    viewNew.classList.toggle('active', name === 'new');
    viewNew.classList.toggle('hidden', name !== 'new');
  }

  function handleBack() {
    if (currentView === 'chat') {
      showView('threads');
      loadThreads();
      ZylBridge.sendToSystem({ type: 'navigation.handled' });
    } else if (currentView === 'new') {
      showView('threads');
      ZylBridge.sendToSystem({ type: 'navigation.handled' });
    } else {
      ZylBridge.sendToSystem({ type: 'navigation.exit' });
    }
  }

  /* ═══════════════════════════════════════════════════════════
     Thread List
     ═══════════════════════════════════════════════════════════ */

  function loadThreads() {
    requestService('messaging', 'getThreads', {});
  }

  onServiceResponse('messaging', 'getThreads', function (data) {
    allThreads = Array.isArray(data) ? data : [];
    threadsEmpty.classList.toggle('hidden', allThreads.length > 0);
    filterAndRenderThreads();
  });

  /* The contact saved with this number. shortcut: the last 7 digits decide, as
     Android does, so +82 10-… and 010-… match; collisions between two contacts
     sharing them pick the first — compare by country rules if that shows up */
  function contactName(number) {
    var want = (number || '').replace(/[^0-9]/g, '').slice(-7);
    if (!want) return '';
    for (var i = 0; i < allContacts.length; i++) {
      var have = (allContacts[i].phone || '').replace(/[^0-9]/g, '').slice(-7);
      if (have === want) return allContacts[i].name || '';
    }
    return '';
  }

  function threadName(t) {
    return contactName(t.number) || t.number || '';
  }

  function avatarColor(name) {
    var code = 0;
    for (var j = 0; j < (name || '').length; j++) code += name.charCodeAt(j);
    return 'avatar-' + (code % 10);
  }

  function filterAndRenderThreads() {
    var query = (threadSearchInput ? threadSearchInput.value.trim().toLowerCase() : '');
    var filtered = allThreads;
    if (query) {
      filtered = [];
      for (var f = 0; f < allThreads.length; f++) {
        var t = allThreads[f];
        var searchable = (threadName(t) + ' ' + (t.number || '') + ' ' + (t.lastMessage || '')).toLowerCase();
        if (searchable.indexOf(query) !== -1) {
          filtered.push(t);
        }
      }
    }
    renderThreads(filtered);
  }

  /* Thread search */
  if (threadSearchInput) {
    threadSearchInput.addEventListener('input', function () {
      filterAndRenderThreads();
    });
  }

  function renderThreads(threads) {
    var html = '';
    for (var i = 0; i < threads.length; i++) {
      var t = threads[i];
      var name = threadName(t);
      var initial = (name || '?').charAt(0).toUpperCase();
      var timeStr = formatRelativeTime(t.lastTime);
      var unreadClass = t.unread ? ' unread' : '';
      var colorClass = avatarColor(name);
      var readIcon = t.unread ? '' : '<span class="thread-read-check" title="' + escapeAttr(zylI18n.t('messages.read')) + '">&#10003;</span>';
      html += '<div class="thread-item' + unreadClass + '" ' +
        'data-thread-id="' + escapeAttr(t.id || '') + '" ' +
        'data-number="' + escapeAttr(t.number || '') + '" ' +
        'data-name="' + escapeAttr(name) + '">' +
        '<div class="thread-avatar ' + colorClass + '">' + escapeHtml(initial) + '</div>' +
        '<div class="thread-info">' +
          '<div class="thread-name">' + escapeHtml(name) + '</div>' +
          '<div class="thread-preview">' + readIcon + escapeHtml(t.lastMessage || '') + '</div>' +
        '</div>' +
        '<div class="thread-meta">' +
          '<div class="thread-time">' + escapeHtml(timeStr) + '</div>' +
          (t.unread ? '<div class="thread-unread-dot"></div>' : '') +
          '<button class="thread-delete-btn" data-delete-id="' + escapeAttr(t.id || '') + '" aria-label="' + escapeAttr(zylI18n.t('messages.delete')) + '">&times;</button>' +
        '</div>' +
      '</div>';
    }
    threadList.innerHTML = html;
  }

  threadList.addEventListener('click', function (e) {
    /* Delete button */
    var deleteBtn = e.target.closest('.thread-delete-btn');
    if (deleteBtn) {
      e.stopPropagation();
      deleteTargetThreadId = deleteBtn.getAttribute('data-delete-id');
      if (deleteDialog) deleteDialog.classList.remove('hidden');
      return;
    }

    var item = e.target.closest('.thread-item');
    if (!item) return;
    currentThreadId = item.getAttribute('data-thread-id');
    currentThreadNumber = item.getAttribute('data-number');
    currentThreadName = item.getAttribute('data-name');
    openChat();
  });

  /* ═══════════════════════════════════════════════════════════
     Delete Conversation
     ═══════════════════════════════════════════════════════════ */

  if (btnDeleteCancel) {
    btnDeleteCancel.addEventListener('click', function () {
      deleteTargetThreadId = null;
      if (deleteDialog) deleteDialog.classList.add('hidden');
    });
  }

  if (btnDeleteConfirm) {
    btnDeleteConfirm.addEventListener('click', function () {
      if (deleteTargetThreadId) {
        requestService('messaging', 'deleteThread', { threadId: deleteTargetThreadId });
        /* Remove from local cache and re-render */
        for (var i = 0; i < allThreads.length; i++) {
          if (allThreads[i].id === deleteTargetThreadId) {
            allThreads.splice(i, 1);
            break;
          }
        }
        filterAndRenderThreads();
        if (allThreads.length === 0) {
          threadsEmpty.classList.remove('hidden');
        }
      }
      deleteTargetThreadId = null;
      if (deleteDialog) deleteDialog.classList.add('hidden');
    });
  }

  /* ═══════════════════════════════════════════════════════════
     Chat View
     ═══════════════════════════════════════════════════════════ */

  function openChat() {
    chatContactName.textContent = currentThreadName || currentThreadNumber;
    chatMessages.innerHTML = '';
    showView('chat');
    loadMessages();

    /* Mark thread as read */
    if (currentThreadId) {
      requestService('messaging', 'markRead', { threadId: currentThreadId });
      /* Update local cache */
      for (var i = 0; i < allThreads.length; i++) {
        if (allThreads[i].id === currentThreadId) {
          allThreads[i].unread = false;
          break;
        }
      }
    }
  }

  function loadMessages() {
    if (!currentThreadId) return;
    requestService('messaging', 'getMessages', { threadId: currentThreadId });
  }

  onServiceResponse('messaging', 'getMessages', function (data) {
    renderMessages(Array.isArray(data) ? data : []);
  });

  /* What the modem said about a sent message: sent, or not sent */
  function statusHtml(m) {
    if (!m.sent || !m.status) return '';
    var failed = m.status === 'failed';
    return '<span class="' + (failed ? 'bubble-failed' : 'bubble-sent') + '">' +
      escapeHtml(zylI18n.t(failed ? 'messages.not_sent' : 'messages.sent')) + '</span>';
  }

  function bubbleHtml(m) {
    return '<div class="bubble ' + (m.sent ? 'sent' : 'received') + '">' +
      escapeHtml(m.text || '') +
      '<div class="bubble-time">' + escapeHtml(formatMessageTime(m.time)) + statusHtml(m) + '</div>' +
    '</div>';
  }

  function renderMessages(messages) {
    var html = '';
    for (var i = 0; i < messages.length; i++) html += bubbleHtml(messages[i]);
    chatMessages.innerHTML = html;
    chatMessages.scrollTop = chatMessages.scrollHeight;
  }

  function appendBubble(m) {
    chatMessages.insertAdjacentHTML('beforeend', bubbleHtml(m));
    chatMessages.scrollTop = chatMessages.scrollHeight;
  }

  /* Send message: shown at once, then redrawn with what the service recorded */
  function sendMessage() {
    var text = chatInput.value.trim();
    if (!text) return;
    var number = currentThreadNumber;
    appendBubble({ text: text, sent: true, time: Date.now() });
    ZylBridge.requestService('messaging', 'send', { number: number, text: text }).then(function (m) {
      if (currentView !== 'chat' || number !== currentThreadNumber) return;
      if (m && m.threadId) currentThreadId = m.threadId;
      loadMessages();
    }, function () {
      if (currentView !== 'chat' || number !== currentThreadNumber) return;
      var last = chatMessages.lastElementChild;
      if (last) last.outerHTML = bubbleHtml({ text: text, sent: true, time: Date.now(), status: 'failed' });
    });
    chatInput.value = '';
    btnSend.classList.remove('active');
  }

  btnSend.addEventListener('click', sendMessage);
  chatInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' || e.keyCode === 13) {
      e.preventDefault();
      sendMessage();
    }
  });
  /* Activate send button when input has text */
  chatInput.addEventListener('input', function () {
    if (chatInput.value.trim()) {
      btnSend.classList.add('active');
    } else {
      btnSend.classList.remove('active');
    }
  });

  btnChatBack.addEventListener('click', function () {
    showView('threads');
    loadThreads();
  });

  /* ═══════════════════════════════════════════════════════════
     Incoming Message
     ═══════════════════════════════════════════════════════════ */

  function handleIncomingMessage(data) {
    if (!data) return;
    /* If currently viewing this thread, append the bubble */
    if (currentView === 'chat' && data.threadId === currentThreadId) {
      appendBubble({ text: data.text, sent: false, time: data.time });
      /* Auto mark as read since user is viewing */
      requestService('messaging', 'markRead', { threadId: currentThreadId });
    } else {
      /* Refresh thread list if visible */
      if (currentView === 'threads') {
        loadThreads();
      }
    }
  }

  ZylBridge.onServiceEvent('messaging', 'MessageReceived', handleIncomingMessage);

  /* ═══════════════════════════════════════════════════════════
     New Message
     ═══════════════════════════════════════════════════════════ */

  btnNewMessage.addEventListener('click', function () {
    showView('new');
    newRecipientInput.value = '';
    renderNewContacts(allContacts);
    loadContacts();
    newRecipientInput.focus();
  });

  btnNewBack.addEventListener('click', function () {
    showView('threads');
  });

  function loadContacts() {
    requestService('contacts', 'getAll', {});
  }

  onServiceResponse('contacts', 'getAll', function (data) {
    allContacts = Array.isArray(data) ? data : [];
    renderNewContacts(allContacts);
    filterAndRenderThreads();
  });

  function renderNewContacts(contacts) {
    var html = '';
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var initial = (c.name || '?').charAt(0).toUpperCase();
      var phone = c.phone || '';
      var cColor = avatarColor(c.name || phone);
      html += '<div class="new-contact-item" data-number="' + escapeAttr(phone) + '" data-name="' + escapeAttr(c.name || phone) + '">' +
        '<div class="new-contact-avatar ' + cColor + '">' + escapeHtml(initial) + '</div>' +
        '<div class="new-contact-info">' +
          '<div class="new-contact-name">' + escapeHtml(c.name || phone) + '</div>' +
          '<div class="new-contact-number">' + escapeHtml(phone) + '</div>' +
        '</div>' +
      '</div>';
    }
    newContactList.innerHTML = html;
  }

  /* Filter contacts as user types */
  newRecipientInput.addEventListener('input', function () {
    var query = newRecipientInput.value.trim().toLowerCase();
    var items = newContactList.querySelectorAll('.new-contact-item');
    for (var i = 0; i < items.length; i++) {
      var name = (items[i].getAttribute('data-name') || '').toLowerCase();
      var number = (items[i].getAttribute('data-number') || '').toLowerCase();
      if (!query || name.indexOf(query) !== -1 || number.indexOf(query) !== -1) {
        items[i].style.display = '';
      } else {
        items[i].style.display = 'none';
      }
    }
  });

  newContactList.addEventListener('click', function (e) {
    var item = e.target.closest('.new-contact-item');
    if (!item) return;
    var number = item.getAttribute('data-number');
    var name = item.getAttribute('data-name');
    startNewThread(number, name);
  });

  /* Allow entering a raw number and pressing Enter */
  newRecipientInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' || e.keyCode === 13) {
      e.preventDefault();
      var raw = newRecipientInput.value.trim();
      if (raw) {
        startNewThread(raw, raw);
      }
    }
  });

  function startNewThread(number, name) {
    currentThreadNumber = number;
    currentThreadName = name || number;
    currentThreadId = null; /* the service answers the first send with it */
    chatContactName.textContent = currentThreadName;
    chatMessages.innerHTML = '';
    showView('chat');
    chatInput.focus();
  }

  /* ═══════════════════════════════════════════════════════════
     Time Formatting
     ═══════════════════════════════════════════════════════════ */

  function formatRelativeTime(ts) {
    if (!ts) return '';
    return zylI18n.formatRelative(ts);
  }

  function formatMessageTime(ts) {
    if (!ts) return '';
    return zylI18n.formatTime(new Date(ts));
  }

  /* ═══════════════════════════════════════════════════════════
     Helpers
     ═══════════════════════════════════════════════════════════ */

  function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escapeAttr(str) {
    return escapeHtml(str).replace(/'/g, '&#39;');
  }

  /* ═══════════════════════════════════════════════════════════
     i18n apply
     ═══════════════════════════════════════════════════════════ */

  function applyI18n() {
    if (typeof zylI18n === 'undefined') return;
    var els = document.querySelectorAll('[data-i18n]');
    for (var i = 0; i < els.length; i++) {
      var key = els[i].getAttribute('data-i18n');
      var val = zylI18n.t(key);
      if (val) els[i].textContent = val;
    }
    var placeholders = document.querySelectorAll('[data-i18n-placeholder]');
    for (var j = 0; j < placeholders.length; j++) {
      var pk = placeholders[j].getAttribute('data-i18n-placeholder');
      var pv = zylI18n.t(pk);
      if (pv) placeholders[j].setAttribute('placeholder', pv);
    }
  }

  window.addEventListener('languagechange', applyI18n);
  applyI18n();

  /* ═══════════════════════════════════════════════════════════
     Init — load thread list
     ═══════════════════════════════════════════════════════════ */

  loadContacts();
  loadThreads();

  /* Another app asked to send a text (app.json "intents"): extras.number is
     the recipient, data the text to start with */
  ZylIntent.onIntent(function (intent) {
    if (intent.action !== ZylIntent.ACTION.SEND) return;
    var number = String((intent.extras && intent.extras.number) || '');
    if (number) startNewThread(number, contactName(number) || number);
    else showView('new');
    if (intent.data) chatInput.value = String(intent.data);
  });

})();
