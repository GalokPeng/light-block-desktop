// An explicit language link takes precedence over the browser preference.
const selected = new URLSearchParams(location.search).get("lang");
if (location.pathname === "/" && selected !== "zh" && !/^zh(?:-|$)/i.test(navigator.language)) {
  location.replace("/en/");
}
