// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 메시지 앱 — 대화목록, 채팅, 새 메시지
// 수행범위: 스레드 목록, 메시지 송수신, 연락처 선택, 시간 포맷
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
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: service,
        method: method,
        params: params || {}
      }), '*');
    }
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

      /* Incoming message notification */
      if (msg.type === 'messaging.received' && msg.data) {
        handleIncomingMessage(msg.data);
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

  /* ═══════════════════════════════════════════════════════════
     State
     ═══════════════════════════════════════════════════════════ */

  var currentView = 'threads';  /* threads | chat | new */
  var currentThreadId = null;
  var currentThreadNumber = '';
  var currentThreadName = '';

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
      window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
    } else if (currentView === 'new') {
      showView('threads');
      window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
    } else {
      window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
    }
  }

  /* ═══════════════════════════════════════════════════════════
     Thread List
     ═══════════════════════════════════════════════════════════ */

  function loadThreads() {
    requestService('messaging', 'getThreads', {});
  }

  onServiceResponse('messaging', 'getThreads', function (data) {
    if (!data || !Array.isArray(data.threads) || data.threads.length === 0) {
      threadList.innerHTML = '';
      threadsEmpty.classList.remove('hidden');
      return;
    }
    threadsEmpty.classList.add('hidden');
    renderThreads(data.threads);
  });

  function renderThreads(threads) {
    var html = '';
    for (var i = 0; i < threads.length; i++) {
      var t = threads[i];
      var initial = (t.name || t.number || '?').charAt(0).toUpperCase();
      var timeStr = formatRelativeTime(t.lastTimestamp);
      var unreadClass = t.unread ? ' unread' : '';
      html += '<div class="thread-item' + unreadClass + '" ' +
        'data-thread-id="' + escapeAttr(t.id || '') + '" ' +
        'data-number="' + escapeAttr(t.number || '') + '" ' +
        'data-name="' + escapeAttr(t.name || t.number || '') + '">' +
        '<div class="thread-avatar">' + escapeHtml(initial) + '</div>' +
        '<div class="thread-info">' +
          '<div class="thread-name">' + escapeHtml(t.name || t.number || '') + '</div>' +
          '<div class="thread-preview">' + escapeHtml(t.lastMessage || '') + '</div>' +
        '</div>' +
        '<div class="thread-meta">' +
          '<div class="thread-time">' + escapeHtml(timeStr) + '</div>' +
          (t.unread ? '<div class="thread-unread-dot"></div>' : '') +
        '</div>' +
      '</div>';
    }
    threadList.innerHTML = html;
  }

  threadList.addEventListener('click', function (e) {
    var item = e.target.closest('.thread-item');
    if (!item) return;
    currentThreadId = item.getAttribute('data-thread-id');
    currentThreadNumber = item.getAttribute('data-number');
    currentThreadName = item.getAttribute('data-name');
    openChat();
  });

  /* ═══════════════════════════════════════════════════════════
     Chat View
     ═══════════════════════════════════════════════════════════ */

  function openChat() {
    chatContactName.textContent = currentThreadName || currentThreadNumber;
    chatMessages.innerHTML = '';
    showView('chat');
    loadMessages();
  }

  function loadMessages() {
    if (!currentThreadId) return;
    requestService('messaging', 'getMessages', { threadId: currentThreadId });
  }

  onServiceResponse('messaging', 'getMessages', function (data) {
    if (!data || !Array.isArray(data.messages)) {
      chatMessages.innerHTML = '';
      return;
    }
    renderMessages(data.messages);
  });

  function renderMessages(messages) {
    var html = '';
    for (var i = 0; i < messages.length; i++) {
      var m = messages[i];
      var bubbleClass = m.sent ? 'sent' : 'received';
      var timeStr = formatMessageTime(m.timestamp);
      html += '<div class="bubble ' + bubbleClass + '">' +
        escapeHtml(m.text || '') +
        '<div class="bubble-time">' + escapeHtml(timeStr) + '</div>' +
      '</div>';
    }
    chatMessages.innerHTML = html;
    chatMessages.scrollTop = chatMessages.scrollHeight;
  }

  function appendBubble(text, isSent) {
    var div = document.createElement('div');
    div.className = 'bubble ' + (isSent ? 'sent' : 'received');

    var textNode = document.createTextNode(text);
    div.appendChild(textNode);

    var timeDiv = document.createElement('div');
    timeDiv.className = 'bubble-time';
    timeDiv.textContent = formatMessageTime(Date.now());
    div.appendChild(timeDiv);

    chatMessages.appendChild(div);
    chatMessages.scrollTop = chatMessages.scrollHeight;
  }

  /* Send message */
  function sendMessage() {
    var text = chatInput.value.trim();
    if (!text) return;
    requestService('messaging', 'send', {
      number: currentThreadNumber,
      threadId: currentThreadId,
      text: text
    });
    appendBubble(text, true);
    chatInput.value = '';
  }

  btnSend.addEventListener('click', sendMessage);
  chatInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' || e.keyCode === 13) {
      e.preventDefault();
      sendMessage();
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
    /* If currently viewing this thread, append the bubble */
    if (currentView === 'chat' && data.threadId === currentThreadId) {
      appendBubble(data.text || '', false);
    } else {
      /* Refresh thread list if visible */
      if (currentView === 'threads') {
        loadThreads();
      }
    }
  }

  /* ═══════════════════════════════════════════════════════════
     New Message
     ═══════════════════════════════════════════════════════════ */

  btnNewMessage.addEventListener('click', function () {
    showView('new');
    newRecipientInput.value = '';
    newContactList.innerHTML = '';
    loadNewContacts();
    newRecipientInput.focus();
  });

  btnNewBack.addEventListener('click', function () {
    showView('threads');
  });

  function loadNewContacts() {
    requestService('contacts', 'getAll', {});
  }

  onServiceResponse('contacts', 'getAll', function (data) {
    if (!data || !Array.isArray(data.contacts)) {
      newContactList.innerHTML = '';
      return;
    }
    renderNewContacts(data.contacts);
  });

  function renderNewContacts(contacts) {
    var html = '';
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var initial = (c.name || '?').charAt(0).toUpperCase();
      var phone = c.phone || c.number || '';
      html += '<div class="new-contact-item" data-number="' + escapeAttr(phone) + '" data-name="' + escapeAttr(c.name || phone) + '">' +
        '<div class="new-contact-avatar">' + escapeHtml(initial) + '</div>' +
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
    currentThreadId = null; /* Service will assign an ID */
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
    var d = new Date(ts);
    var now = new Date();
    var diffMs = now.getTime() - d.getTime();
    var diffMin = Math.floor(diffMs / 60000);
    var t = typeof zylI18n !== 'undefined' ? zylI18n : null;

    if (diffMin < 1) {
      return t ? (t.t('messages.just_now') || 'Just now') : 'Just now';
    }
    if (diffMin < 60) return diffMin + 'm';
    var diffH = Math.floor(diffMin / 60);
    if (diffH < 24) return diffH + 'h';
    /* Check if yesterday */
    var yesterday = new Date(now);
    yesterday.setDate(yesterday.getDate() - 1);
    if (d.toDateString() === yesterday.toDateString()) {
      return t ? (t.t('messages.yesterday') || 'Yesterday') : 'Yesterday';
    }
    var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    return pad(d.getMonth() + 1) + '/' + pad(d.getDate());
  }

  function formatMessageTime(ts) {
    if (!ts) return '';
    var d = new Date(ts);
    var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    return pad(d.getHours()) + ':' + pad(d.getMinutes());
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

  loadThreads();

})();
