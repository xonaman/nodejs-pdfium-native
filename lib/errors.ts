/** Base error for all PDFium operations. */
export class PDFiumError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'PDFiumError';
    this.code = code;
  }
}

/** Thrown when the file cannot be found or opened. */
export class PDFiumFileError extends PDFiumError {
  constructor(message: string) {
    super('FILE', message);
    this.name = 'PDFiumFileError';
  }
}

/** Thrown when the input is not a valid PDF or is corrupted. */
export class PDFiumFormatError extends PDFiumError {
  constructor(message: string) {
    super('FORMAT', message);
    this.name = 'PDFiumFormatError';
  }
}

/** Thrown when a password is required or incorrect. */
export class PDFiumPasswordError extends PDFiumError {
  constructor(message: string) {
    super('PASSWORD', message);
    this.name = 'PDFiumPasswordError';
  }
}

/** Thrown when the PDF uses an unsupported security scheme. */
export class PDFiumSecurityError extends PDFiumError {
  constructor(message: string) {
    super('SECURITY', message);
    this.name = 'PDFiumSecurityError';
  }
}

export function parseNativeError(err: unknown): PDFiumError {
  const msg = err instanceof Error ? err.message : String(err);
  const colonIdx = msg.indexOf(':');
  if (colonIdx === -1) return new PDFiumError('UNKNOWN', msg);

  const code = msg.slice(0, colonIdx);
  const text = msg.slice(colonIdx + 1);

  switch (code) {
    case 'FILE':
      return new PDFiumFileError(text);
    case 'FORMAT':
      return new PDFiumFormatError(text);
    case 'PASSWORD':
      return new PDFiumPasswordError(text);
    case 'SECURITY':
      return new PDFiumSecurityError(text);
    default:
      return new PDFiumError(code, text);
  }
}
