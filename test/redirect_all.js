
(function(document) {
  'use strict'

  const urlAttributes = ['href', 'src', 'srcset', 'action', 'data', 'style']
  const styleUrlAttributes = ['background', 'backgroundImage',
    'borderImage', 'borderImageSource', 'listStyle', 'listStyleImage']

  function patchUrl (url) {
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
      encodeURIComponent(__webrecorder.origin))
    url = url.split(encodeURIComponent(window.location.host)).join(
      encodeURIComponent(__webrecorder.host))
    url = url.split(encodeURIComponent(window.location.hostname)).join(
      encodeURIComponent(__webrecorder.hostname))

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
      url = __webrecorder.origin + url
    } else {
      var isHttp = /^https?:/i
      if (!isHttp.test(url)) {
        return url
      }
    }

    // try to convert back to relative
    if (url.startsWith(__webrecorder.server_base)) {
      url = url.substring(__webrecorder.server_base.length)
    } else {
      // patch absolute to relative
      url = '/' + url
    }
    return url
  }

  function patchStyleUrl (url) {
    const match = url.match(/url\('([^']+)'\)/) ||
                  url.match(/url\("([^']+)"\)/) ||
                  url.match(/url\(([^\)]+)\)/)
    if (match) {
      url = url.replace(match[1], patchUrl(match[1]))
    }
    return url
  }

  if (false) {
    const patchUrl_ = patchUrl
    patchUrl = function (url) {
      const patched = patchUrl_(url)
      if (url !== patched) {
        console.log('patched ' + url + ' to ' + patched)
      }
      return patched
    }

    const patchStyleUrl_ = patchStyleUrl
    patchStyleUrl = function (url) {
      const patched = patchStyleUrl_(url)
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
      return open.apply(this, arguments)
    }
  }

  function patchFetch () {
    const fetch = window.fetch
    window.fetch = function () {
      arguments[0] = patchUrl(arguments[0])
      return fetch.apply(this, arguments)
    }
  }

  function patchAttribute (element, attribute) {
    if (attribute === 'style') {
      if (element.style) {
        for (const styleAttribute of styleUrlAttributes) {
          const url = element.style[styleAttribute]
          if (url) {
            const patchedUrl = patchStyleUrl(url)
            if (url !== patchedUrl) {
              element.style[styleAttribute] = patchedUrl
            }
          }
        }
      }
    }
    else {
      const url = element[attribute]
      if (url) {
        const patchedUrl = patchUrl(url)
        if (url !== patchedUrl) {
          element[attribute] = patchedUrl
        }
      }
    }
  }

  function patchElement (element) {
    if (element.tagName === 'IFRAME') {
      if (iframe.contentWindow) {
        patchDocument(iframe.contentWindow.document)
      }
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

  function patchDocument (document) {
    const observer = new MutationObserver(
      function (mutations) {
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
      })

    observer.observe(document, {
      childList: true,
      subtree: true,
      characterData: false,
      attributes: true,
      attributeFilter: urlAttributes
    })

    const head = document.head
    if (head) {
      for (let child = head.firstElementChild; child; child = child.nextElementSibling) {
        for (let attribute of ['href', 'src']) {
          patchAttribute(child, attribute)
        }
      }
    }

    const body = document.body
    if (body) {
      const children = []
      while (body.firstChild) {
        children.push(body.firstChild)
        removeChild(body.firstChild)
      }
      for (const child of children) {
        body.appendChild(child)
      }
    }
  }

  patchXHR()
  patchFetch()
  patchDocument(document)

})(document)
