// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - UI Component
//
// 역할: 키보드 예측 변환 모듈 — bigram 기반 다음 단어 후보 제공
// 수행범위: 영어 상위 500단어 + 한글 300단어 빈도 사전,
//          bigram 테이블, 입력 컨텍스트 추적, 후보 3개 반환
// 의존방향: 없음 (순수 JS 모듈, keyboard.js 에서 optional require)
// SOLID: SRP — 예측 변환 로직만 담당
//        OCP — 언어 사전 추가로 확장 가능
//        DIP — keyboard.js 가 인터페이스(getPredictions)만 호출
//
// ES5 문법만 사용. hunspell 의존성 없음.
// ──────────────────────────────────────────────────────────

window.ZylPrediction = (function () {
  'use strict';

  /* ─── 영어 상위 500 단어 빈도 사전 ─── */
  /* rank: 낮을수록 자주 쓰임 */
  var EN_WORDS = [
    'the','be','to','of','and','a','in','that','have','it',
    'for','not','on','with','he','as','you','do','at','this',
    'but','his','by','from','they','we','say','her','she','or',
    'an','will','my','one','all','would','there','their','what','so',
    'up','out','if','about','who','get','which','go','me','when',
    'make','can','like','time','no','just','him','know','take','people',
    'into','year','your','good','some','could','them','see','other','than',
    'then','now','look','only','come','its','over','think','also','back',
    'after','use','two','how','our','work','first','well','way','even',
    'new','want','because','any','these','give','day','most','us','great',
    'between','need','large','often','hand','high','place','hold','turn','help',
    'here','where','much','through','long','down','should','never','each','own',
    'world','still','nation','tell','old','man','follow','came','show','every',
    'good','home','both','before','might','few','school','same','always','near',
    'hard','start','might','story','saw','far','sea','draw','left','late',
    'run','keep','talk','stop','without','second','later','miss','idea','enough',
    'eat','face','watch','far','indian','real','almost','let','above','girl',
    'sometimes','mountain','cut','young','talk','soon','list','song','being','leave',
    'family','body','music','color','stand','sun','questions','fish','area','mark',
    'horse','birds','problem','complete','room','knew','since','ever','piece','told',
    'usually','didn','friends','easy','heard','order','red','door','sure','become',
    'top','ship','across','today','during','short','better','best','however','low',
    'hours','black','products','happened','whole','measure','remember','early','waves','reached',
    'listen','wind','rock','space','covered','fast','several','hold','himself','toward',
    'five','step','morning','passed','vowel','true','hundred','against','pattern','numeral',
    'table','north','slowly','money','map','farm','pulled','draw','voice','power',
    'town','fine','drive','led','cry','dark','machine','note','waiting','plan',
    'figure','star','box','noun','field','rest','able','pound','done','beauty',
    'drive','stood','contain','front','teach','week','final','gave','green','quick',
    'develop','ocean','warm','free','minute','strong','special','behind','clear','tail',
    'produce','fact','street','inch','multiply','nothing','course','stay','wheel','full',
    'force','blue','object','decide','surface','deep','moon','island','foot','system',
    'busy','test','record','boat','common','gold','possible','plane','age','dry',
    'wonder','laugh','thousand','ago','ran','check','game','shape','equate','hot',
    'miss','brought','heat','snow','tire','bring','yes','distant','fill','east',
    'paint','language','among','grand','ball','yet','wave','drop','heart','am',
    'present','heavy','dance','engine','position','arm','wide','sail','material','size',
    'vary','settle','speak','weight','general','ice','matter','circle','pair','include',
    'divide','syllable','felt','perhaps','pick','sudden','count','square','reason','length',
    'represent','art','subject','region','energy','hunt','probable','bed','brother','egg',
    'ride','cell','believe','perhaps','contain','cause','interest','stood','single','speak',
    'rather','length','speed','fly','wait','century','select','guess','experience','score',
    'capital','cotton','process','sleep','prove','condition','feed','consider','temperature','agree',
    'shoulder','forward','sentence','mean','better','busy','glass','trade','word','nature',
    'method','exact','sharp','thin','plain','air','compound','proper','relation','suppose'
  ];

  /* ─── 한글 300 단어 빈도 사전 ─── */
  var KO_WORDS = [
    '나','너','우리','그','그녀','이것','저것','여기','저기','어디',
    '무엇','어떻게','왜','언제','누가','하다','있다','없다','되다','오다',
    '가다','보다','말하다','알다','모르다','좋다','싫다','크다','작다','빠르다',
    '느리다','많다','적다','새','오래','지금','아까','나중','먼저','같이',
    '혼자','함께','모두','일부','전체','그리고','하지만','그러나','그래서','따라서',
    '만약','비록','예를','들면','즉','또한','게다가','반면','한편','결국',
    '학교','집','회사','병원','가게','시장','공원','도서관','역','공항',
    '식당','카페','마트','은행','약국','우체국','경찰서','소방서','유치원','대학',
    '사람','친구','가족','부모','자녀','형제','자매','선생','학생','의사',
    '변호사','경찰','군인','운동선수','음악가','화가','작가','요리사','배우','가수',
    '사랑','행복','슬픔','기쁨','화','두려움','놀람','혐오','신뢰','기대',
    '음식','물','밥','빵','고기','생선','채소','과일','음료','술',
    '커피','차','우유','주스','와인','맥주','소주','막걸리','떡','라면',
    '컴퓨터','핸드폰','텔레비전','자동차','자전거','기차','버스','비행기','배','오토바이',
    '책','신문','잡지','편지','이메일','문자','전화','인터넷','소셜','미디어',
    '날씨','봄','여름','가을','겨울','맑음','흐림','비','눈','바람',
    '더위','추위','습도','기온','태풍','지진','홍수','가뭄','안개','구름',
    '돈','경제','정치','사회','문화','역사','과학','기술','예술','스포츠',
    '축구','야구','농구','테니스','수영','달리기','등산','낚시','독서','여행',
    '영화','음악','미술','문학','연극','무용','사진','만화','게임','유튜브',
    '건강','운동','다이어트','영양','수면','스트레스','명상','요가','필라테스','힐링',
    '행운','성공','실패','노력','열정','꿈','목표','계획','결과','과정',
    '시간','공간','장소','방향','거리','높이','넓이','크기','무게','속도',
    '빨강','파랑','녹색','노랑','주황','보라','검정','하양','회색','갈색',
    '하나','둘','셋','넷','다섯','여섯','일곱','여덟','아홉','열',
    '첫째','둘째','셋째','마지막','처음','끝','중간','앞','뒤','옆',
    '위','아래','안','밖','왼쪽','오른쪽','동','서','남','북',
    '아침','점심','저녁','밤','낮','오전','오후','새벽','주말','평일',
    '월요일','화요일','수요일','목요일','금요일','토요일','일요일','이번','다음','지난',
    '작년','올해','내년','옛날','미래','현재','과거','요즘','최근','곧'
  ];

  /* ─── bigram 테이블 (상위 사용 패턴) ─── */
  /* {선행단어: [후보1, 후보2, 후보3, ...]} */
  var EN_BIGRAMS = {
    'i':      ['am','will','have','can','want','think','know','was','would','need'],
    'you':    ['are','can','will','have','should','might','need','want','know','could'],
    'the':    ['world','best','most','first','last','same','only','next','new','other'],
    'a':      ['new','good','great','small','big','long','little','few','lot','bit'],
    'it':     ['is','was','will','can','should','might','could','has','had','seems'],
    'we':     ['are','will','have','can','should','need','want','must','could','might'],
    'they':   ['are','will','have','can','were','should','would','could','might','need'],
    'he':     ['is','was','will','has','had','would','could','should','might','can'],
    'she':    ['is','was','will','has','had','would','could','should','might','can'],
    'this':   ['is','was','will','has','can','would','should','might','could','means'],
    'that':   ['is','was','will','has','can','would','should','might','could','means'],
    'can':    ['be','do','help','get','make','see','find','use','take','go'],
    'will':   ['be','have','not','help','make','get','see','find','go','take'],
    'have':   ['a','the','to','been','had','not','some','many','no','any'],
    'what':   ['is','are','do','does','did','the','you','we','they','can'],
    'how':    ['are','is','to','do','can','much','many','long','often','about'],
    'please': ['let','help','make','note','find','provide','ensure','send','check','keep'],
    'good':   ['morning','evening','night','day','job','luck','work','idea','time','thing'],
    'thank':  ['you','them','everyone','her','him','god','all'],
    'hello':  ['world','there','everyone','friend','dear'],
    'my':     ['name','family','friend','home','work','life','heart','mind','goal','plan'],
    'not':    ['sure','the','a','only','just','always','really','quite','very','at'],
    'do':     ['you','not','the','a','it','that','this','we','they','your'],
    'in':     ['the','a','this','that','order','fact','general','addition','case','time'],
    'on':     ['the','a','this','that','my','your','our','its','top','time'],
    'at':     ['the','a','this','that','home','work','school','least','last','first'],
    'is':     ['the','a','not','it','this','that','very','really','quite','just'],
    'are':    ['you','we','they','the','a','not','very','really','quite','just']
  };

  var KO_BIGRAMS = {
    '나':    ['는','를','의','에게','도','만','까지','부터','처럼','같이'],
    '너':    ['는','를','의','에게','도','만','까지','부터','처럼','가'],
    '우리':  ['는','가','를','의','에게','도','함께','모두','같이','나라'],
    '지금':  ['은','는','부터','까지','당장','바로','여기','이','그','저'],
    '오늘':  ['은','는','도','부터','까지','아침','저녁','밤','낮','하루'],
    '내일':  ['은','는','도','부터','까지','아침','저녁','오전','오후','일찍'],
    '이것':  ['은','는','이','가','을','를','도','만','때문에','처럼'],
    '그':    ['리고','래서','러나','것','때','곳','사람','분','는','가'],
    '하지만':['이','그','저','더','좀','잘','못','안','다시','새로'],
    '그래서':['이','그','저','더','좀','잘','못','안','다시','새로'],
    '정말':  ['로','이','대단해','감사해','좋아','싫어','맞아','그래','어려워','재밌어'],
    '매우':  ['좋다','중요하다','크다','많다','적다','어렵다','쉽다','빠르다','느리다','강하다'],
    '아주':  ['좋다','중요하다','크다','많다','적다','어렵다','쉽다','빠르다','느리다','강하다'],
    '좋은':  ['것','사람','일','날','아침','저녁','방법','생각','시간','소식'],
    '새로운':['것','사람','일','날','방법','생각','기술','아이디어','시작','도전'],
    '감사':  ['합니다','해요','드려요','드립니다','했어요','드렸어요'],
    '안녕':  ['하세요','하십니까','히','하십시오','하셔요'],
    '반갑':  ['습니다','어요','게','다'],
    '좋아':  ['요','해요','합니다','했어요','했습니다'],
    '싫어':  ['요','해요','합니다','했어요','했습니다'],
    '이':    ['것','사람','곳','분','번','날','시','때','런','렇'],
    '그것':  ['은','는','이','가','을','를','도','만','때문에','처럼'],
    '모든':  ['것','사람','곳','일','방법','시간','힘','마음','가능성','노력']
  };

  /* ─── 상태 ─── */
  var _lastWord = '';
  var _currentInput = '';

  /* ─── 내부 유틸 ─── */

  function normalize(word) {
    return word.toLowerCase().replace(/[^a-z0-9가-힣]/g, '');
  }

  function isKorean(word) {
    return /[가-힣]/.test(word);
  }

  function filterByPrefix(candidates, prefix) {
    if (!prefix) return candidates;
    var result = [];
    for (var i = 0; i < candidates.length; i++) {
      if (candidates[i].indexOf(prefix) === 0) {
        result.push(candidates[i]);
      }
    }
    return result;
  }

  function getWordFreqList(lang) {
    return lang === 'ko' ? KO_WORDS : EN_WORDS;
  }

  /* ─── bigram 후보 조회 ─── */
  function getBigramCandidates(prevWord, currentPrefix, lang) {
    var norm = normalize(prevWord);
    var bigramMap = (lang === 'ko') ? KO_BIGRAMS : EN_BIGRAMS;
    var candidates = bigramMap[norm] || [];
    if (currentPrefix) {
      candidates = filterByPrefix(candidates, currentPrefix);
    }
    return candidates.slice(0, 3);
  }

  /* ─── 빈도 기반 후보 조회 (bigram miss fallback) ─── */
  function getFreqCandidates(prefix, lang) {
    var wordList = getWordFreqList(lang);
    if (!prefix) return wordList.slice(0, 3);
    var result = [];
    for (var i = 0; i < wordList.length && result.length < 3; i++) {
      if (wordList[i].indexOf(prefix) === 0) {
        result.push(wordList[i]);
      }
    }
    return result;
  }

  /* ─── 공개 API ─── */

  /**
   * 현재 입력된 텍스트 컨텍스트 업데이트.
   * keyboard.js 가 키 입력마다 호출.
   * @param {string} text - 현재 입력 필드 전체 텍스트
   */
  function updateContext(text) {
    _currentInput = text || '';
    var words = _currentInput.trim().split(/\s+/);
    if (words.length >= 2) {
      _lastWord = words[words.length - 2];
      _currentInput = words[words.length - 1];
    } else if (words.length === 1) {
      _lastWord = '';
      _currentInput = words[0];
    } else {
      _lastWord = '';
      _currentInput = '';
    }
  }

  /**
   * 예측 후보 반환.
   * @param {string} lang - 'en' | 'ko'
   * @returns {string[]} 최대 3개 후보
   */
  function getPredictions(lang) {
    var useLang = (lang === 'ko' || isKorean(_currentInput || _lastWord)) ? 'ko' : 'en';

    /* 1) bigram 기반 후보 */
    if (_lastWord) {
      var candidates = getBigramCandidates(_lastWord, _currentInput, useLang);
      if (candidates.length > 0) return candidates;
    }

    /* 2) 빈도 사전 prefix 매칭 */
    var freqCandidates = getFreqCandidates(_currentInput, useLang);
    if (freqCandidates.length > 0) return freqCandidates;

    /* 3) 언어 전환 fallback (ko ↔ en) */
    var altLang = (useLang === 'ko') ? 'en' : 'ko';
    return getFreqCandidates(_currentInput, altLang).slice(0, 3);
  }

  /**
   * 후보를 선택했을 때 컨텍스트 리셋.
   * @param {string} chosen - 선택된 후보 단어
   */
  function onCandidateSelected(chosen) {
    _lastWord = chosen;
    _currentInput = '';
  }

  /**
   * 컨텍스트 초기화 (입력 필드 클리어 시 호출).
   */
  function reset() {
    _lastWord = '';
    _currentInput = '';
  }

  return {
    updateContext: updateContext,
    getPredictions: getPredictions,
    onCandidateSelected: onCandidateSelected,
    reset: reset
  };
})();
