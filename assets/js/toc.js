document.addEventListener("DOMContentLoaded", function() {
  var tocContainer = document.getElementById('toc-sidebar');
  // Try to get content container, compatible with different Jekyll themes
  var contentContainer = document.querySelector('.content');

  if (!contentContainer || !tocContainer) {
    return;
  }

  // 1. Generate TOC
  var headings = contentContainer.querySelectorAll('h1, h2, h3, h4');

  if (headings.length === 0) return; // If no headings, don't display

  var tocHtml = '<h3>Index</h3><ul class="toc-level-1">';
  var currentLevel = null; // Start from the first heading level
  var stack = [1]; // Track current nested level

  headings.forEach(function(heading, index) {
    if (heading.className == 'postTitle') {
      return;
    }

    // If heading has no ID, auto-generate one for anchor navigation
    if (!heading.id) {
      heading.id = 'section-' + index;
    }

    var level = parseInt(heading.tagName.substring(1));

    // Initialize first level
    if (currentLevel === null) {
      currentLevel = level;
    }

    // If level increases, open new ul
    if (level > currentLevel) {
      // Open ul level by level, ensure each level has corresponding ul
      for (var i = currentLevel + 1; i <= level; i++) {
        tocHtml += '<ul class="toc-level-' + i + '">';
        stack.push(i);
      }
    } else if (level < currentLevel) {
      // If level decreases, close extra ul
      while (stack.length > 1 && stack[stack.length - 1] > level) {
        tocHtml += '</ul>';
        stack.pop();
      }
    }

    tocHtml += '<li><a href="#' + heading.id + '">' + heading.innerText + '</a></li>';
    currentLevel = level;
  });

  // Close all remaining ul
  while (stack.length > 1) {
    tocHtml += '</ul>';
    stack.pop();
  }
  tocHtml += '</ul>';
  tocContainer.innerHTML = tocHtml;

  // 2. Handle toggle button logic
  var toggleBtn = document.getElementById('toc-toggle-btn');

  if (toggleBtn) {
    toggleBtn.addEventListener('click', function() {
      document.body.classList.toggle('toc-open');
    });
  }

  // 3. Scroll highlight logic
  window.addEventListener('scroll', function() {
    var scrollPos = window.scrollY + 100; // 100px offset

    headings.forEach(function(heading) {
      if (scrollPos >= heading.offsetTop) {
        var link = tocContainer.querySelector('a[href="#' + heading.id + '"]');
        if (link) {
          // Remove other highlights
          tocContainer.querySelectorAll('a').forEach(a => a.classList.remove('active'));
          // Add current highlight
          link.classList.add('active');
        }
      }
    });
  });
});
