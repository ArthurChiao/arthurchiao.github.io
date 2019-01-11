---
layout: default
title: Articles
---

<div id="articles">
  <h1>Articles</h1>
  <ul class="posts noList">
    {% for post in site.posts %}
      <li>
        <span>
          <span class="date">{{ post.date | date_to_string }}</span>
          <h4><a href="{{ post.url }}">{{ post.title }}</a></h4>
        </span>
      </li>
    {% endfor %}
  </ul>
</div>
