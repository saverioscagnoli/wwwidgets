wwwidgets.state.onChange("ui.theme", (theme) => {
  document.documentElement.dataset.theme = theme;
});
