import eslint from '@eslint/js';
import tseslint from 'typescript-eslint';
import prettier from 'eslint-config-prettier';
import globals from 'globals';

export default tseslint.config(
  { ignores: ['build/', 'deps/', 'dist/', 'node_modules/'] },
  eslint.configs.recommended,
  ...tseslint.configs.recommended,
  // Type-aware linting for the published library source. Catches the
  // async-native-addon bug class (floating/misused promises, unsafe `any`).
  {
    files: ['lib/**/*.ts'],
    extends: [...tseslint.configs.recommendedTypeChecked],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
  },
  // Build/tooling scripts and examples run on Node without type info.
  {
    files: ['scripts/**/*.{js,mjs,cjs}', 'examples/**/*.{js,mjs,cjs}', 'test/**/*.{js,mjs,cjs}'],
    languageOptions: {
      globals: globals.node,
    },
  },
  {
    files: ['**/*.test.ts'],
    rules: {
      'no-empty': 'off',
    },
  },
  // Keep last: disable stylistic rules that conflict with Prettier.
  prettier,
);
