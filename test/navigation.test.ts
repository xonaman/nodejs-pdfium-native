import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';
import { allLoaded } from './helpers.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// Exact scripts embedded by scripts/generate-fixtures.mjs.
const OPEN_ACTION_JS = 'app.alert("Opened");';
const HELPER_JS = 'function helper() { return 42; }';

describe('PDFiumDocument.getJavaScriptActions', () => {
  it('returns an empty array for a document without JavaScript', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(await doc.getJavaScriptActions()).toEqual([]);
    doc.destroy();
  });

  it('lists each action with its name and script', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const actions = allLoaded(await doc.getJavaScriptActions());

    expect(actions).toHaveLength(2);
    expect(actions.map((a) => a.index)).toEqual([0, 1]);

    const open = actions.find((a) => a.name === 'OpenAction')!;
    expect(open.script).toBe(OPEN_ACTION_JS);

    const helper = actions.find((a) => a.name === 'Helper')!;
    expect(helper.script).toBe(HELPER_JS);

    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    doc.destroy();
    await expect(doc.getJavaScriptActions()).rejects.toThrow('Document is destroyed');
  });
});

describe('PDFiumDocument.getNamedDestinations', () => {
  it('returns an empty array for a document without destinations', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    expect(await doc.getNamedDestinations()).toEqual([]);
    doc.destroy();
  });

  it('resolves each destination name to a page index', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    expect(dests.map((d) => d.name).sort()).toEqual(['Alpha', 'Beta', 'Legacy']);
    expect(dests.find((d) => d.name === 'Alpha')!.pageIndex).toBe(0);
    expect(dests.find((d) => d.name === 'Beta')!.pageIndex).toBe(1);

    doc.destroy();
  });

  it('reads destinations from the legacy /Dests catalog dictionary too', async () => {
    // 'Legacy' lives in the old-style catalog /Dests dict, not the name tree
    const doc = await loadDocument(fixture('navigation.pdf'));
    const legacy = (await doc.getNamedDestinations()).find((d) => d.name === 'Legacy')!;

    expect(legacy.pageIndex).toBe(2);
    expect(legacy.view).toBe('fitH');
    expect(legacy.viewParams).toEqual([250]);

    doc.destroy();
  });

  it('reports the fit type and its parameters', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    const alpha = dests.find((d) => d.name === 'Alpha')!;
    expect(alpha.view).toBe('xyz');
    expect(alpha.viewParams).toEqual([50, 700, 2]);

    // 'fit' takes no parameters at all
    const beta = dests.find((d) => d.name === 'Beta')!;
    expect(beta.view).toBe('fit');
    expect(beta.viewParams).toEqual([]);

    doc.destroy();
  });

  it('exposes x/y/zoom only for xyz destinations that specify them', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const dests = await doc.getNamedDestinations();

    const alpha = dests.find((d) => d.name === 'Alpha')!;
    expect(alpha.destX).toBe(50);
    expect(alpha.destY).toBe(700);
    expect(alpha.destZoom).toBe(2);

    const beta = dests.find((d) => d.name === 'Beta')!;
    expect(beta.destX).toBeUndefined();
    expect(beta.destY).toBeUndefined();
    expect(beta.destZoom).toBeUndefined();

    doc.destroy();
  });

  it('rejects after the document is destroyed', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    doc.destroy();
    await expect(doc.getNamedDestinations()).rejects.toThrow('Document is destroyed');
  });
});

// A document-open script that cannot be decoded is more interesting to a
// triage caller than no script at all, so it is reported as a null entry
// rather than dropped.
describe('PDFiumDocument.getJavaScriptActions with an unloadable action', () => {
  it('reports a null for every action that fails, including a trailing one', async () => {
    const doc = await loadDocument(fixture('dangling-javascript.pdf'));

    const actions = await doc.getJavaScriptActions();

    // two of the three fail, and one of them is the last entry — a trailing
    // failure is the shape most likely to be lost, since the array length
    // comes from the allocation rather than from the final assignment
    expect(actions).toHaveLength(3);
    expect(actions[1]).toBeNull();
    expect(actions[2]).toBeNull();

    expect(actions[0]).toBeTruthy();
    expect(actions[0]!.index).toBe(0);
    expect(actions[0]!.name).toBe('script_alpha');

    // "no scripts" and "scripts I could not read" are distinguishable
    expect(actions.filter((a) => a === null)).toHaveLength(2);

    doc.destroy();
  });

  it('leaves an intact document free of null entries', async () => {
    const doc = await loadDocument(fixture('navigation.pdf'));
    const actions = await doc.getJavaScriptActions();
    expect(actions.length).toBeGreaterThan(0);
    expect(actions.every((a) => a !== null)).toBe(true);
    doc.destroy();
  });
});
