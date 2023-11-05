Repo for my [blog](https://arthurchiao.github.io). Why blogging? Some hints:

* [Writing Is Magic, 2022](https://brooker.co.za/blog/2022/11/08/writing.html),
  中文版 [长文写作的魔力](https://arthurchiao.art/blog/writing-is-magic-zh)

# 1. Theme

Originated from [Long Haul](http://github.com/brianmaierjr/long-haul),
and has evolved significantly since then.

# 2. Use this theme/template

1. Create your own repo on github

    Repo name must be: `<your_github_id>.github.io`.

2. Clone this repo, remove my info

    ```shell
    $ git clone https://github.com/arthurchiao/arthurchiao.github.io.git
    $ cp -rf arthurchiao.github.io <your_github_id>.github.io
    $ cd <your_github_id>.github.io
    
    # remove my posts
    $ rm -rf .git/ _posts/* _drafts/*
    
    # replace personal information in the matched files
    $ grep -i arthurchiao *
    ```

    **Remove or replace the google analytics code in `_config.yml`**, otherwise,
    your website's statistics will go to mine:

    ```shell
    $ vim _config.yml # delete this line: google_analytics: "G-5SFP2TKSL7"
                      # or replace it with your own code
    ```

3. Add your own posts

    ```
    $ touch _posts/2023-11-05-your-first-post.md
    ```

    add following content to the beginning of the post:

    ```
    ---
    layout    : post
    title     : "Title of Your First Post"
    date      : 2023-11-05
    lastupdate: 2023-11-05
    categories: tag1 tag2 tag3
    ---
    ```

    then, add the content of your post in markdown format.

4. Local preview

    ```shell
    $ cd <your_github_id>.github.io
    $ bundle exec jekyll serve --drafts --incremental
    ```

    Visit `localhost:4000` with your browser.

5. Push to github

    ```shell
    $ git init && git add .
    $ git config user.name "YOUR_NAME"
    $ git config user.mail "YOUR_EMAIL"
    $ git commit -m "Initial commit"

    $ git remote add origin https://github.com/<your_github_id>/<your_github_id>.github.io
    $ git push origin master
    ```

    Visit `https://<your_github_id>.github.io`.

Happy blogging!
