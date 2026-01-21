#include "owl_dev_elements.h"
#include "logger.h"
#include "../resources/icons/icons.h"
#include <sstream>
#include <iomanip>

OwlDevElements::OwlDevElements() {
  LOG_DEBUG("DevElements", "Elements tab initialized");
}

OwlDevElements::~OwlDevElements() {
}

void OwlDevElements::AddElement(const DOMElement& element) {
  std::lock_guard<std::mutex> lock(elements_mutex_);
  elements_.push_back(element);
}

void OwlDevElements::ClearElements() {
  std::lock_guard<std::mutex> lock(elements_mutex_);
  elements_.clear();
}

std::string OwlDevElements::GetDOMTreeJSON() {
  std::lock_guard<std::mutex> lock(elements_mutex_);

  std::ostringstream json;
  json << "[";

  for (size_t i = 0; i < elements_.size(); i++) {
    const auto& elem = elements_[i];

    if (i > 0) json << ",";
    json << "{";
    json << "\"tag\":\"" << elem.tag << "\",";
    json << "\"id\":\"" << elem.id << "\",";
    json << "\"classes\":\"" << elem.classes << "\",";
    json << "\"text\":\"" << elem.text_content << "\",";
    json << "\"depth\":" << elem.depth;
    json << "}";
  }

  json << "]";
  return json.str();
}

std::string OwlDevElements::GenerateHTML() {
  std::ostringstream html;

  html << R"HTML(
<div class="elements-container">
  <div class="elements-toolbar">
    <button class="btn btn-secondary" onclick="refreshElements()">
      )HTML" << OlibIcons::ARROWS_ROTATE << R"HTML(
      Refresh DOM
    </button>
    <div style="flex: 1;"></div>
    <input type="text" class="filter-input" id="elements-filter" placeholder="Filter elements..." oninput="filterElements()">
  </div>

  <div class="elements-tree" id="elements-tree">
    <div class="empty-state">Click "Refresh DOM" to inspect elements</div>
  </div>

  <div class="elements-inspector" id="elements-inspector">
    <div class="inspector-section">
      <div class="inspector-title">Properties</div>
      <div id="element-properties" class="inspector-content">
        <div class="empty-state">Select an element to view properties</div>
      </div>
    </div>
  </div>
</div>

<style>
  .elements-container {
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow: hidden;
  }

  .elements-toolbar {
    background: #252526;
    border-bottom: 1px solid #3c3c3c;
    padding: 8px 12px;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .elements-tree {
    flex: 2;
    overflow-y: auto;
    font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
    font-size: 12px;
    padding: 8px;
    background: #1e1e1e;
    border-bottom: 1px solid #3c3c3c;
  }

  .elements-inspector {
    flex: 1;
    overflow-y: auto;
    background: #252526;
  }

  .inspector-section {
    padding: 8px;
  }

  .inspector-title {
    font-size: 11px;
    font-weight: 600;
    color: #cccccc;
    margin-bottom: 8px;
    padding: 4px 8px;
    background: #1e1e1e;
    border-radius: 3px;
  }

  .inspector-content {
    font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
    font-size: 11px;
    color: #cccccc;
  }

  .dom-element {
    padding: 2px 4px;
    cursor: pointer;
    white-space: nowrap;
  }

  .dom-element:hover {
    background: #2a2a2a;
  }

  .dom-element.selected {
    background: #094771;
  }

  .dom-closing-tag {
    opacity: 0.6;
    cursor: default;
  }

  .dom-closing-tag:hover {
    background: transparent;
  }

  .dom-tag {
    color: #569cd6;
  }

  .dom-attr-name {
    color: #9cdcfe;
  }

  .dom-attr-value {
    color: #ce9178;
  }

  .dom-text {
    color: #cccccc;
  }
</style>

<script>
  let domElements = [];

  function refreshElements() {
    // Request DOM tree from main page by triggering extraction
    console.log('__OLIB_REFRESH_ELEMENTS__');

    const container = document.getElementById('elements-tree');
    container.innerHTML = '<div class="empty-state">Loading DOM tree...</div>';
  }

  // Function called from C++ to update DOM tree
  function updateDOMTree(elementsData) {
    try {
      domElements = JSON.parse(elementsData);
      renderElements();
    } catch (e) {
      console.error('Failed to parse DOM data:', e);
      const container = document.getElementById('elements-tree');
      container.innerHTML = '<div class="empty-state">Error loading DOM tree: ' + e.message + '</div>';
    }
  }

  function renderElements() {
    const container = document.getElementById('elements-tree');
    if (domElements.length === 0) {
      container.innerHTML = '<div class="empty-state">Click "Refresh DOM" to inspect elements</div>';
      return;
    }

    // Build tree structure with closing tags
    let html = '';
    let depthStack = []; // Track elements that need closing tags

    for (let index = 0; index < domElements.length; index++) {
      const elem = domElements[index];
      const indent = '&nbsp;&nbsp;'.repeat(elem.depth);
      const idAttr = elem.id ? ` <span class="dom-attr-name">id</span>=<span class="dom-attr-value">"${escapeHtml(elem.id)}"</span>` : '';
      const classAttr = elem.classes ? ` <span class="dom-attr-name">class</span>=<span class="dom-attr-value">"${escapeHtml(elem.classes)}"</span>` : '';

      // Show additional attributes (format: name="value")
      let otherAttrs = '';
      if (elem.attrs && elem.attrs.length > 0) {
        otherAttrs = elem.attrs.map(attr => {
          const eqPos = attr.indexOf('=');
          if (eqPos > 0) {
            const attrName = attr.substring(0, eqPos);
            const attrValue = attr.substring(eqPos + 1);
            return ` <span class="dom-attr-name">${escapeHtml(attrName)}</span>=<span class="dom-attr-value">${escapeHtml(attrValue)}</span>`;
          } else {
            return ` <span class="dom-attr-name">${escapeHtml(attr)}</span>`;
          }
        }).join('');
      }

      // Escape text content for HTML display
      const textContent = elem.text ? ` <span class="dom-text">${escapeHtml(elem.text)}</span>` : '';

      // Close tags that are at higher or same depth (going back up the tree)
      while (depthStack.length > 0 && depthStack[depthStack.length - 1].depth >= elem.depth) {
        const closing = depthStack.pop();
        const closeIndent = '&nbsp;&nbsp;'.repeat(closing.depth);
        html += `
          <div class="dom-element dom-closing-tag">
            ${closeIndent}&lt;/<span class="dom-tag">${closing.tag}</span>&gt;
          </div>
        `;
      }

      // Render opening tag
      let tagDisplay = '';
      if (elem.isVoid) {
        // Self-closing tag
        tagDisplay = `${indent}&lt;<span class="dom-tag">${elem.tag}</span>${idAttr}${classAttr}${otherAttrs} /&gt;`;
      } else if (elem.childCount > 0) {
        // Tag with children - will need closing tag later
        tagDisplay = `${indent}&lt;<span class="dom-tag">${elem.tag}</span>${idAttr}${classAttr}${otherAttrs}&gt;${textContent}`;
        depthStack.push({ tag: elem.tag, depth: elem.depth });
      } else {
        // Empty tag - close immediately
        tagDisplay = `${indent}&lt;<span class="dom-tag">${elem.tag}</span>${idAttr}${classAttr}${otherAttrs}&gt;${textContent}&lt;/<span class="dom-tag">${elem.tag}</span>&gt;`;
      }

      html += `
        <div class="dom-element" onclick="selectElement(${index})" data-index="${index}">
          ${tagDisplay}
        </div>
      `;
    }

    // Close any remaining open tags at the end
    while (depthStack.length > 0) {
      const closing = depthStack.pop();
      const closeIndent = '&nbsp;&nbsp;'.repeat(closing.depth);
      html += `
        <div class="dom-element dom-closing-tag">
          ${closeIndent}&lt;/<span class="dom-tag">${closing.tag}</span>&gt;
        </div>
      `;
    }

    container.innerHTML = html;
  }

  function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
  }

  function selectElement(index) {
    // Remove previous selection
    document.querySelectorAll('.dom-element').forEach(el => el.classList.remove('selected'));

    // Add selection to clicked element
    document.querySelector(`.dom-element[data-index="${index}"]`).classList.add('selected');

    // Show element properties
    const elem = domElements[index];

    // Format other attributes if available
    let attrsDisplay = '';
    if (elem.attrs && elem.attrs.length > 0) {
      attrsDisplay = '<div style="margin-top: 8px;"><strong>Other Attributes:</strong></div>';
      attrsDisplay += '<div style="margin-left: 8px; font-size: 10px; font-family: monospace;">';
      attrsDisplay += elem.attrs.map(attr => {
        const eqPos = attr.indexOf('=');
        if (eqPos > 0) {
          const attrName = attr.substring(0, eqPos);
          const attrValue = attr.substring(eqPos + 1);
          return `<div style="margin-bottom: 2px;"><span style="color: #9cdcfe;">${escapeHtml(attrName)}</span>=<span style="color: #ce9178;">${escapeHtml(attrValue)}</span></div>`;
        } else {
          return `<div style="margin-bottom: 2px; color: #9cdcfe;">${escapeHtml(attr)}</div>`;
        }
      }).join('');
      attrsDisplay += '</div>';
    }

    const propsHtml = `
      <div style="padding: 8px;">
        <div><strong>Tag:</strong> &lt;${elem.tag}&gt;</div>
        ${elem.id ? `<div><strong>ID:</strong> ${escapeHtml(elem.id)}</div>` : ''}
        ${elem.classes ? `<div><strong>Classes:</strong> ${escapeHtml(elem.classes)}</div>` : ''}
        ${elem.text ? `<div style="margin-top: 8px;"><strong>Text Content:</strong></div><div style="margin-left: 8px; font-size: 10px; color: #cccccc; max-height: 200px; overflow-y: auto; white-space: pre-wrap; background: #1e1e1e; padding: 4px; border-radius: 3px;">${escapeHtml(elem.text)}</div>` : ''}
        <div style="margin-top: 8px;"><strong>Children:</strong> ${elem.childCount || 0}</div>
        <div><strong>Depth:</strong> ${elem.depth}</div>
        ${elem.isVoid ? '<div><strong>Type:</strong> Void Element (self-closing)</div>' : ''}
        ${attrsDisplay}
      </div>
    `;

    document.getElementById('element-properties').innerHTML = propsHtml;
  }

  function filterElements() {
    const filter = document.getElementById('elements-filter').value.toLowerCase();
    const elements = document.querySelectorAll('.dom-element');

    elements.forEach(el => {
      const text = el.textContent.toLowerCase();
      el.style.display = text.includes(filter) ? 'block' : 'none';
    });
  }
</script>
)HTML";

  return html.str();
}
