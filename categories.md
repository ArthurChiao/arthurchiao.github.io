---
layout: default
title: Categories
---

<div id="archives">
{% for category in site.categories %}
  <div class="archive-group">
    {% capture category_name %}{{ category | first }}{% endcapture %}
    <div id="#{{ category_name | slugize }}"></div>
    <h4 class="category-head">{{ category_name }}</h4>
    {% for post in site.categories[category_name] %}
      <article class="archive-item">
        <span class="date">{{ post.date | date: "%Y-%m-%d" }}</span>
        <a style="text-decoration:none" href="{{ post.url }}">{{post.title}}</a>
      </article>
    {% endfor %}
  </div>
{% endfor %}
</div>
