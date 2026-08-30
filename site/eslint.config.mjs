import astro from "eslint-plugin-astro";
import tsParser from "@typescript-eslint/parser";
import globals from "globals";
import tseslint from "typescript-eslint";

const sharedGlobals = {
  ...globals.browser,
  ...globals.node,
};

export default tseslint.config(
  {
    ignores: [
      "**/node_modules/**",
      "build/**",
      "editors/vscode/out/**",
      "out/**",
      "site/.astro/**",
      "site/dist/**",
      "site/generated/**",
      "site/src/content/docs/stdlib/**",
    ],
  },
  ...tseslint.configs.recommended,
  ...astro.configs.recommended,
  {
    files: ["**/*.astro"],
    languageOptions: {
      parserOptions: {
        parser: tsParser,
      },
    },
  },
  {
    files: ["**/*.{astro,js,mjs,ts,tsx}"],
    languageOptions: {
      ecmaVersion: "latest",
      globals: sharedGlobals,
      sourceType: "module",
    },
    rules: {
      eqeqeq: "error",
      "no-constant-condition": "error",
      "no-debugger": "error",
      "no-duplicate-imports": "error",
      "no-fallthrough": "error",
      "no-irregular-whitespace": "error",
      "no-new-wrappers": "error",
      "no-self-assign": "error",
      "no-shadow-restricted-names": "error",
      "no-unreachable": "error",
      "no-unused-labels": "error",
      "no-useless-catch": "error",
      "no-useless-escape": "error",
      "no-var": "error",
      "prefer-const": "error",
    },
  },
  {
    files: ["**/*.{js,mjs}"],
    rules: {
      "@typescript-eslint/no-require-imports": "off",
      "no-undef": "error",
      "no-unused-vars": ["error", { argsIgnorePattern: "^_" }],
    },
  },
);
