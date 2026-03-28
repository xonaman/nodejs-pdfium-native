import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('PDFiumPage.getFormFields', () => {
  it('returns an empty array for a page with no form fields', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();
    expect(Array.isArray(fields)).toBe(true);
    expect(fields.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('returns all form fields from the form-fields fixture', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();
    // text(2) + checkbox(2) + radio(3) + dropdown(1) + listbox(1) = 9
    expect(fields.length).toBeGreaterThanOrEqual(9);
    page.close();
    doc.destroy();
  });

  it('reads text field with value', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const fullName = fields.find((f) => f.name === 'fullName');
    expect(fullName).toBeDefined();
    expect(fullName!.type).toBe('textField');
    expect(fullName!.value).toBe('John Doe');
    expect(fullName!.bounds).toBeDefined();

    page.close();
    doc.destroy();
  });

  it('reads empty text field', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const email = fields.find((f) => f.name === 'email');
    expect(email).toBeDefined();
    expect(email!.type).toBe('textField');
    expect(email!.value).toBe('');

    page.close();
    doc.destroy();
  });

  it('reads checked and unchecked checkboxes', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const agree = fields.find((f) => f.name === 'agree');
    expect(agree).toBeDefined();
    expect(agree!.type).toBe('checkbox');
    expect(agree!.isChecked).toBe(true);

    const newsletter = fields.find((f) => f.name === 'newsletter');
    expect(newsletter).toBeDefined();
    expect(newsletter!.type).toBe('checkbox');
    expect(newsletter!.isChecked).toBe(false);

    page.close();
    doc.destroy();
  });

  it('reads radio button group', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const radios = fields.filter((f) => f.name === 'color');
    expect(radios.length).toBe(3);
    expect(radios.every((r) => r.type === 'radioButton')).toBe(true);

    // one should be checked (green was selected)
    const checked = radios.filter((r) => r.isChecked);
    expect(checked.length).toBe(1);

    page.close();
    doc.destroy();
  });

  it('reads combo box with options', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const country = fields.find((f) => f.name === 'country');
    expect(country).toBeDefined();
    expect(country!.type).toBe('comboBox');
    expect(country!.value).toBe('France');
    expect(country!.options).toBeDefined();
    expect(country!.options!.length).toBe(4);
    expect(country!.options!.map((o) => o.label)).toEqual(['Germany', 'France', 'Italy', 'Spain']);

    page.close();
    doc.destroy();
  });

  it('reads list box with options', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    const fruits = fields.find((f) => f.name === 'fruits');
    expect(fruits).toBeDefined();
    expect(fruits!.type).toBe('listBox');
    expect(fruits!.options).toBeDefined();
    expect(fruits!.options!.length).toBe(4);
    expect(fruits!.options!.map((o) => o.label)).toEqual(['Apple', 'Banana', 'Cherry', 'Date']);

    // Cherry should be selected
    const selected = fruits!.options!.filter((o) => o.isSelected);
    expect(selected.length).toBe(1);
    expect(selected[0].label).toBe('Cherry');

    page.close();
    doc.destroy();
  });

  it('all fields have flags property', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    const fields = await page.getFormFields();

    for (const field of fields) {
      expect(typeof field.flags).toBe('number');
    }

    page.close();
    doc.destroy();
  });
});
