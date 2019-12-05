return R"(
;(function (document) {
  const urlAttributes = ['href', 'src', 'srcset', 'action', 'data']
  const urlElements = ['a', 'img', 'script', 'link', 'iframe', 'form', 'input',
    'source', 'embed', 'object', 'audio', 'video']

  function patchUrl (url, isLink) {
    if (typeof (url) !== 'string') {
      return url
    }
    url = url.trim()

    // remove http://127.0.0.1:port from beginning
    if (url.startsWith(window.location.origin)) {
      url = url.substring(window.location.origin.length)
    }
    // replace [http://]127.0.0.1[:port] in the middle
    url = url.split(encodeURIComponent(window.location.origin)).join(
      encodeURIComponent(__webrecorder_origin))
    url = url.split(encodeURIComponent(window.location.host)).join(
      encodeURIComponent(__webrecorder_host))
    url = url.split(encodeURIComponent(window.location.hostname)).join(
      encodeURIComponent(__webrecorder_hostname))

    // convert to absolute
    if (url.startsWith('//')) {
      url = window.location.protocol + url
    } else if (url.startsWith('/')) {
      if (url.startsWith('/#')) {
        return url.substring(1)
      }
      var isPatched = /^\/https?:/i
      if (isPatched.test(url)) {
        return url
      }
      url = __webrecorder_origin + url
    } else {
      var isHttp = /^https?:/i
      if (!isHttp.test(url)) {
        return url
      }
    }

    if (!isLink || __webrecorder_follow_link.test(url)) {
      // try to convert back to relative
      if (url.startsWith(__webrecorder_server_base)) {
        url = url.substring(__webrecorder_server_base.length)
      } else {
        // patch absolute to relative
        url = '/' + url
      }
    }
    return url
  }

  if (false) {
    const patchUrl_ = patchUrl
    patchUrl = function (url, isLink) {
      const patched = patchUrl_(url, isLink)
      if (url !== patched) {
        console.log('patched ' + url + ' to ' + patched)
      }
      return patched
    }
  }

  function patchXHR () {
    const open = XMLHttpRequest.prototype.open
    XMLHttpRequest.prototype.open = function () {
      arguments[1] = patchUrl(arguments[1])
      open.apply(this, arguments)
    }
  }

  function patchSetter (prototype, attribute, isLink) {
    const attrib = Object.getOwnPropertyDescriptor(prototype, attribute)
    if (attrib) {
      Object.defineProperty(prototype, attribute, {
        set: function (value) { attrib.set.call(this, patchUrl(value, isLink)) },
        get: function (value) { return attrib.get.call(this) }
      })
    }
  }

  const setAttribute = Element.prototype.setAttribute

  function patchElementSetAttribute () {
    Element.prototype.setAttribute = function () {
      for (const attribute of urlAttributes) {
        if (arguments[0] === attribute) {
          arguments[1] = patchUrl(arguments[1])
        }
      }
      setAttribute.apply(this, arguments)
    }
  }

  function patchAttribute (element, attribute) {
    const isLink = (element.tagName === 'A')
    let value = element[attribute]
    value = patchUrl(value, isLink)
    setAttribute.apply(element, [attribute, value])
  }

  function patchIFrame (iframe) {
    if (!iframe.contentWindow) {
      return
    }
    observeDocument(iframe.contentWindow.document)

    const body = iframe.contentWindow.document.body
    const children = []
    while (body.firstChild) {
      children.push(body.firstChild)
      body.removeChild(body.firstChild)
    }
    for (const child of children) {
      body.appendChild(child)
    }
  }

  function patchElement (element) {
    if (element.tagName === 'IFRAME') {
      patchIFrame(element)
    }
    for (const attribute of urlAttributes) {
      if (element[attribute]) {
        patchAttribute(element, attribute)
      }
    }
    for (const child of element.children) {
      patchElement(child)
    }
  }

  function observeDocument (document, skipInitial) {
    const observer = new MutationObserver(
      function (mutations) {
        if (!skipInitial) {
          mutations.forEach(function (mutation) {
            if (mutation.addedNodes) {
              for (const node of mutation.addedNodes) {
                if (node.nodeType === Node.ELEMENT_NODE) {
                  patchElement(node)
                }
              }
            } else {
              patchAttribute(mutation.target, mutation.attributeName)
            }
          })
        }
      })

    observer.observe(document, {
      childList: true,
      subtree: true,
      characterData: false,
      attributes: true,
      attributeFilter: urlAttributes
    })

    if (skipInitial) {
      document.addEventListener('DOMContentLoaded',
        function () { skipInitial = false }, false)
    }
  }

  function onCookieSet (cookie) {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', '__webrecorder_setcookie')
    xhr.setRequestHeader('Content-Type', 'text/plain')
    xhr.send(cookie)
  }

  function patchSetCookie () {
    Object.defineProperty(document, 'cookie', {
      get: function () {
        return __webrecorder_cookies
      },
      set: function (cookie) {
        const map = { }
        const set = function (c) {
          const p = c.indexOf('=')
          const key = c.substring(0, p).trim()
          const value = c.substring(p + 1).trim()
          if (value.length > 0) {
            map[key] = value
          } else {
            delete map[key]
          }
        }
        for (const c of __webrecorder_cookies.split(';')) { set(c) }
        for (const c of decodeURIComponent(cookie).split(';')) {
          set(c)
          break
        }
        const array = []
        Object.keys(map).forEach(
          function (key, index) { array.push(key + '=' + map[key]) })
        const cookies = array.join('; ')
        if (__webrecorder_cookies !== cookies) {
          __webrecorder_cookies = cookies
          onCookieSet(cookie)
        }
      }
    })
  }

  function patchDateNow () {
    const dateNow = Date.now
    const date = Date

    // for the first second keep returning a constant time
    const startTime = dateNow() + 1000

    Date = function () { return new date(Date.now()) }
    Date.prototype = date.prototype
    Date.UTC = date.UTC
    Date.parse = date.parse
    Date.now = function () {
      return __webrecorder_start_time + Math.max(0, dateNow() - startTime)
    }
  }

  function patchMathRandom () {
    const seeds = {}
    Math.random = function () {
      const location = new Error().stack
      const hashCode =
        location.split('').reduce((a, b) => (((a << 5) - a) + b.charCodeAt(0)) | 0, 0)
      let seed = (seeds[hashCode] || 0)
      seed = (1103515245 * seed + 12345) % 0x7FFFFFFF
      seeds[hashCode] = seed
      return seed / 0x7FFFFFFF
    }
  }

  patchXHR()
  patchSetCookie()
  patchDateNow()
  patchMathRandom()

  // patch settings of url attributes
  patchElementSetAttribute()
  for (const element of urlElements) {
    var prototype = Object.getPrototypeOf(document.createElement(element))
    for (const attribute of urlAttributes) {
      patchSetter(prototype, attribute, (element === 'a'))
    }
  }

  // use observer to patch innerHTML, document.write, iframe... afterwards
  observeDocument(document, true)
})(document)
)";
