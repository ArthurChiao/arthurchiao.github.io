Repo for my [blog](https://arthurchiao.github.io).

# 1. Theme

The theme originates from [Long Haul](http://github.com/brianmaierjr/long-haul),
and has changed a lot since then.

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
    your website's statistics will go to my mine:

    ```shell
    $ vim _config.yml # delete this line: google_analytics: "UA-89960542-1"
                      # or replace it with your own code
    ```

3. Add your own posts

    ```
    $ touch _posts/2019-11-09-your-first-post.md
    ```

    add following content to the beginning of the post:

    ```
    ---
    layout    : post
    title     : "Title of Your First Post"
    date      : 2019-11-09
    lastupdate: 2019-11-09
    categories: tag1 tag2 tag3
    ---
    ```

    after that, add the content of your post in markdown format.

4. Local development

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
