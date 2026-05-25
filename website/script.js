// BadBlood-Revival — landing page
//
// API_URL is read from window.BB_API_URL if set (drop a <script> with
// `window.BB_API_URL = "https://pls.yourdomain.it"` before script.js to
// override), otherwise falls back to the page's origin (same-host deploy),
// and finally to localhost for dev.

const API_URL = (typeof window.BB_API_URL === 'string' && window.BB_API_URL)
    || (location.origin && location.origin !== 'null' ? location.origin : 'http://localhost');

// ── Live Stats ──────────────────────────────────
async function updateStats() {
    try {
        const res = await fetch(`${API_URL}/api/version`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: '{}'
        });
        const data = await res.json();

        // Main stats section
        document.getElementById('stat-status').textContent = 'ONLINE';
        document.getElementById('stat-status').className = 'stat-val stat-online';
        document.getElementById('stat-version').textContent = 'v' + data.version;

        // Nav version
        document.getElementById('nav-version').textContent = 'ver. ' + data.version;

        // HUD bar
        document.getElementById('hud-status').textContent = 'ONLINE';
        document.getElementById('hud-bar-fill').style.width = '100%';
    } catch (e) {
        document.getElementById('stat-status').textContent = 'OFFLINE';
        document.getElementById('stat-status').className = 'stat-val stat-offline';
        document.getElementById('hud-status').textContent = 'OFFLINE';
        document.getElementById('hud-bar-fill').style.width = '0%';
    }

    try {
        const res = await fetch(`${API_URL}/api/stats`);
        const data = await res.json();
        if (data.players !== undefined) {
            document.getElementById('stat-players').textContent = data.players;
            document.getElementById('hud-players').textContent = data.players;
        }
        if (data.registered !== undefined) {
            document.getElementById('stat-registered').textContent = data.registered;
            document.getElementById('hud-registered').textContent = data.registered;
        }
    } catch (e) {
        document.getElementById('stat-players').textContent = '—';
        document.getElementById('stat-registered').textContent = '—';
    }
}

// ── Smooth scroll for nav links ─────────────────
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        e.preventDefault();
        const target = document.querySelector(this.getAttribute('href'));
        if (target) target.scrollIntoView({ behavior: 'smooth', block: 'start' });
    });
});

// ── Active tab tracking on scroll ───────────────
const sections = document.querySelectorAll('section[id]');
const navTabs = document.querySelectorAll('.nav-tab');

function updateActiveTab() {
    const scrollPos = window.scrollY + 120;
    let currentId = 'hero';

    sections.forEach(section => {
        if (section.offsetTop <= scrollPos) {
            currentId = section.id;
        }
    });

    navTabs.forEach(tab => {
        const href = tab.getAttribute('href');
        if (href === '#' + currentId) {
            tab.classList.add('active');
        } else {
            tab.classList.remove('active');
        }
    });
}

window.addEventListener('scroll', updateActiveTab);

// ── Nav background intensity on scroll ──────────
window.addEventListener('scroll', () => {
    const nav = document.querySelector('nav');
    if (window.scrollY > 60) {
        nav.style.background = 'rgba(10, 10, 14, 0.98)';
    } else {
        nav.style.background = 'rgba(10, 10, 14, 0.95)';
    }
});

// ── Fade-in animations ──────────────────────────
const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            entry.target.classList.add('visible');
        }
    });
}, { threshold: 0.1 });

document.querySelectorAll('.panel-section, #hero').forEach(el => {
    el.classList.add('fade-in');
    observer.observe(el);
});

// ── Footer "BACK TO TOP" ────────────────────────
const footerRight = document.querySelector('.footer-right');
if (footerRight) {
    footerRight.addEventListener('click', () => {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });
}

// ── Feature card hover highlight ────────────────
document.querySelectorAll('.game-card').forEach(card => {
    card.addEventListener('mouseenter', () => {
        document.querySelectorAll('.game-card').forEach(c => c.classList.remove('active'));
        card.classList.add('active');
    });
});

// ── Init ─────────────────────────────────────────
updateStats();
setInterval(updateStats, 30000);
updateActiveTab();
