{
  const start = runtimeJs.search(/let\s+chartFrame\s*=/), end = runtimeJs.indexOf('function fixedChartTicks(', start);
  if (start < 0 || end < 0) throw new Error('Chart label layout helpers are missing');
  const roots = [], frames = [];
  const fakeDocument = {
    createElement() {
      return {
        dataset: {}, style: {}, textContent: '', className: '',
        setAttribute() {},
        remove() { const p = this.parent; if (p) p.children = p.children.filter(c => c !== this); },
        get classList() { return {contains: name => this.className === name}; },
        getBoundingClientRect() {
          const root = this.parent, width = this.textContent.length * 7;
          const x = root.width * parseFloat(this.style.left) / 100;
          const transform = this.style.transform ||
              (root.children[0] === this ? 'translateX(0)' :
               root.children.at(-1) === this ? 'translateX(-100%)' : 'translateX(-50%)');
          const left = x - width * (transform.includes('-100%') ? 1 : transform.includes('-50%') ? .5 : 0);
          return {left, right: left + width, top: 0, bottom: 10};
        },
      };
    },
    querySelectorAll() { return roots.filter(root => root.dataset.chartAxis !== undefined); },
  };
  const {fillChartTicks, layoutChartLabels} = new Function(
      'document', 'ResizeObserver', 'requestAnimationFrame', '$',
      runtimeJs.slice(start, end) + ';return {fillChartTicks,layoutChartLabels}')(
      fakeDocument, class { observe() {} }, cb => frames.push(cb), () => ({}));
  const root = width => {
    const e = {
      width, dataset: {}, children: [],
      setAttribute(name, value) { this[name] = value; },
      removeAttribute(name) { delete this[name]; },
      replaceChildren() { this.children = []; },
      appendChild(child) { child.parent = this; this.children.push(child); },
      querySelectorAll() { return this.children; },
      getBoundingClientRect() { return {left: 0, right: this.width, top: 0, bottom: 16, width: this.width, height: 16}; },
    };
    roots.push(e); return e;
  };
  const flush = () => { while (frames.length) frames.shift()(); };
  const visible = e => e.children.filter(n => n.style.visibility === 'visible');
  const verify = e => {
    const labels = visible(e);
    for (const n of labels) {
      const r = n.getBoundingClientRect();
      if (r.left < -1 || r.right > e.width + 1) throw new Error('A chart label is clipped');
      for (const other of labels) {
        if (other === n) continue;
        const q = other.getBoundingClientRect();
        if (r.left < q.right + 4 && q.left < r.right + 4)
          throw new Error('Chart labels overlap');
      }
    }
  };
  const bar = root(343);
  for (const weight of [35.9, 36, 36.1, 42]) {
    if (weight === 42) bar.width = 400;
    fillChartTicks(bar, [[0, '0 g', 80, 'zero'], [36, '36 g', 100, 'goal'],
      [weight, weight + ' g', 20, 'actual']], Math.max(36, weight), true);
    flush(); verify(bar);
    const shown = visible(bar).map(n => n.textContent);
    if (!shown.includes('36 g') || !shown.includes('0 g') ||
        (weight === 42 && !shown.includes('42 g')) ||
        (weight !== 42 && shown.includes('35.9 g')) ||
        (weight !== 42 && shown.includes('36.1 g')))
      throw new Error('Goal, zero, and actual-weight label priorities changed at ' + weight + ' g: ' + shown);
  }
  fillChartTicks(bar, [], 0);
  if (bar.children.length || bar.role || bar['aria-label'] || 'chartAxis' in bar.dataset || bar._shown)
    throw new Error('Cleared shot retains chart ticks or accessible weight labels');
  fillChartTicks(bar, [[0, '0 g', 80, 'zero'], [36, '36 g', 100, 'goal']], 36, true);
  flush(); verify(bar);
  if (visible(bar).length !== 2) throw new Error('Chart labels did not recover after clearing');
  const time = root(288);
  fillChartTicks(time, [[0, '0 s', 80, 'zero'], [27.9, '27.9 s', 20, 'fast'],
    [28, '28 s', 20, 'bbw'], [28.1, '28.1 s', 20, 'slow'], [60, '60 s', 70, 'end']], 60, true);
  flush(); verify(time);
  if (visible(time).length >= time.children.length) throw new Error('Close guard labels were not filtered');
  time.width = 144;
  layoutChartLabels(); flush(); verify(time);

  const chart = root(343);
  fillChartTicks(chart, [[0, '0 g', 80, 'zero'], [34, '34 g', 20, 'slow'],
    [36, '36 g', 100, 'goal'], [42.5, '42.5 g', 70, 'max']], 42.5, true, true);
  flush(); verify(chart);
  {
    const shown = visible(chart).map(n => n.textContent);
    const range = chart.children.find(n => n.dataset.m);
    if (!shown.includes('0 g') || !shown.includes('34–36–42.5 g') ||
        shown.includes('34 g') || shown.includes('36 g') ||
        shown.includes('42.5 g') || !range ||
        Math.abs(parseFloat(range.style.left) - 90) > 0.01)
      throw new Error('Crammed labels must merge into one centered range: ' + shown);
  }
  fillChartTicks(chart, [[0, '0 g', 80, 'zero'], [36, '36 g', 100, 'goal'],
    [40, '40 g', 70, 'max']], 40, true, true);
  flush(); verify(chart);
  {
    const shown = visible(chart).map(n => n.textContent);
    const range = chart.children.find(n => n.dataset.m);
    if (!shown.includes('0 g') || !shown.includes('36–40 g') ||
        shown.includes('36 g') || shown.includes('40 g') || !range ||
        Math.abs(parseFloat(range.style.left) - 95) > 0.01)
      throw new Error('Goal near axis max must merge into one range: ' + shown);
  }
  fillChartTicks(chart, [[0, '0 s', 80, 'zero'], [26, '26 s', 20, 'fast'],
    [50, '50 s', 70, 'end']], 50, true, true);
  flush(); verify(chart);
  if (visible(chart).length !== 3 || chart.children.some(n => n.dataset.m))
    throw new Error('Spaced labels must not merge');
  const flex = root(500);
  fillChartTicks(flex, [[0, '0 g', 80, 'zero'], [36, '36 g', 100, 'goal'],
    [40, '40 g', 70, 'max']], 40, true, true);
  flush(); verify(flex);
  if (visible(flex).length !== 3 || flex.children.some(n => n.dataset.m))
    throw new Error('Wide layouts must not merge');
  flex.width = 343;
  layoutChartLabels(); flush(); verify(flex);
  {
    const shown = visible(flex).map(n => n.textContent);
    if (!shown.includes('36–40 g') || !shown.includes('0 g') ||
        shown.includes('36 g') || shown.includes('40 g'))
      throw new Error('Re-layout after shrinking must merge newly colliding labels: ' + shown);
  }
  flex.width = 500;
  layoutChartLabels(); flush(); verify(flex);
  if (visible(flex).length !== 3 || flex.children.some(n => n.dataset.m))
    throw new Error('Re-layout after widening must drop stale merged labels');
  fillChartTicks(chart, [], 0, true, true);
  if (chart.children.length || 'chartMerge' in chart.dataset)
    throw new Error('Cleared chart must drop ticks and the merge opt-in');
  const solo = root(343);
  fillChartTicks(solo, [[0, '0 g', 80, 'zero'], [34, '34 g', 20, 'slow'],
    [36, '36 g', 100, 'goal']], 36, true);
  flush(); verify(solo);
  {
    const shown = visible(solo).map(n => n.textContent);
    if (shown.includes('34–36 g') || shown.includes('34 g') ||
        !shown.includes('36 g') || solo.children.some(n => n.dataset.m))
      throw new Error('Axes without the merge opt-in must keep priority hiding: ' + shown);
  }
  const tiny = root(40);
  fillChartTicks(tiny, [[0, '0 g', 80, 'zero'], [34, '34 g', 20, 'slow'],
    [36, '36 g', 100, 'goal']], 36, true, true);
  flush(); verify(tiny);
  {
    const shown = visible(tiny).map(n => n.textContent);
    if (shown.length !== 1 || shown[0] !== '36 g')
      throw new Error('A merged range that cannot fit must fall back: ' + shown);
  }
  if (!codeIncludes(runtimeJs, "fillChartTicks($('ruleChartTicks')") &&
      !codeIncludes(runtimeJs, 'fillChartTicks(t,'))
    throw new Error('Chart tick rendering must stay shared with the shot charts');
}
