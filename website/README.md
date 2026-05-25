# website/

Static landing page for the project. Pure HTML/CSS/JS, no build step,
deploy anywhere.

- `index.html` — the page
- `style.css` — styling
- `script.js` — fetches live stats (`/api/version`, `/api/stats`) from
  the server set via `window.BB_API_URL` (or the page's own origin if
  served from the same host as the server)
- `assets/` — images, fonts

## Deploying

If the website lives on the same host as the API (`pls.yourdomain.it`),
just drop these files at the web root and serve them with nginx /
Caddy / any static server.

If you host the site on a different domain (e.g. GitHub Pages while the
API is on your VPS), point at the API explicitly:

```html
<!-- in index.html, BEFORE script.js -->
<script>
    window.BB_API_URL = "https://pls.yourdomain.it";
</script>
<script src="script.js"></script>
```
