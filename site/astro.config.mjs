// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import { readFileSync } from 'node:fs';

const yonaGrammar = JSON.parse(
	readFileSync(new URL('./grammars/yona.tmLanguage.json', import.meta.url), 'utf-8'),
);

export default defineConfig({
	site: 'https://yona-lang.org',
	integrations: [
		starlight({
			title: 'Yona',
			description:
				'Yona is a statically typed functional programming language compiled to native code via LLVM — transparent concurrency, algebraic effects, linear resources, and performance within reach of C.',
			social: [
				{ icon: 'github', label: 'GitHub', href: 'https://github.com/yona-lang/yonac-llvm' },
			],
			head: [
				{
					tag: 'script',
					attrs: {
						defer: true,
						'data-domain': 'yona-lang.org',
						src: 'https://plausible.kiket.dev/js/script.file-downloads.hash.outbound-links.pageview-props.revenue.tagged-events.js',
					},
				},
				{
					tag: 'script',
					content:
						'window.plausible = window.plausible || function() { (window.plausible.q = window.plausible.q || []).push(arguments) }',
				},
			],
			customCss: [
				'@fontsource/fraunces/400.css',
				'@fontsource/fraunces/600.css',
				'@fontsource/fraunces/700.css',
				'@fontsource/source-sans-3/400.css',
				'@fontsource/source-sans-3/600.css',
				'@fontsource/ibm-plex-mono/400.css',
				'@fontsource/ibm-plex-mono/500.css',
				'./src/styles/theme.css',
			],
			expressiveCode: {
				shiki: {
					langs: [yonaGrammar],
					langAlias: { yonai: 'yona', Yona: 'yona' },
				},
				styleOverrides: {
					borderRadius: '0.4rem',
					codeFontFamily: "'IBM Plex Mono', ui-monospace, monospace",
				},
			},
			sidebar: [
				{
					label: 'Start',
					items: [
						{ label: 'Why Yona 2.0', slug: 'why-yona-2' },
						{ label: 'Installation', slug: 'install' },
						{ label: 'Quick start', slug: 'learn/quick-start' },
					],
				},
				{
					label: 'Learn',
					items: [
						{ label: 'Syntax and evaluation', slug: 'learn/syntax' },
						{ label: 'Functions', slug: 'learn/functions' },
						{ label: 'Pattern matching', slug: 'learn/pattern-matching' },
						{ label: 'Types and data', slug: 'learn/types' },
						{ label: 'Collections', slug: 'learn/collections' },
						{ label: 'Concurrency', slug: 'learn/concurrency' },
						{ label: 'Effects', slug: 'learn/effects' },
						{ label: 'Modules', slug: 'learn/modules' },
						{ label: 'Style', slug: 'learn/style' },
					],
				},
				{
					label: 'Guides',
					items: [
						{ label: 'Concurrency in depth', slug: 'guides/concurrency' },
						{ label: 'The type system', slug: 'guides/type-system' },
						{ label: 'Memory and linearity', slug: 'guides/memory' },
						{ label: 'Persistent data structures', slug: 'guides/persistent-data-structures' },
						{ label: 'Traits', slug: 'guides/traits' },
						{ label: 'Modules and interfaces', slug: 'guides/modules-interfaces' },
						{ label: 'Iterators and streams', slug: 'guides/iterators' },
						{ label: 'Accelerators (GPU)', slug: 'guides/accelerators' },
						{ label: 'Performance', slug: 'guides/performance' },
					],
				},
				{
					label: 'Reference',
					items: [
						{ label: 'Language specification', slug: 'reference/specification' },
						{ label: 'Prelude', slug: 'reference/prelude' },
						{ label: 'Compiler CLI', slug: 'reference/cli' },
						{ label: 'Error codes', slug: 'reference/error-codes' },
					],
				},
				{
					label: 'Standard library',
					collapsed: true,
					items: [{ autogenerate: { directory: 'stdlib' } }],
				},
				{
					label: 'For AI agents',
					items: [{ label: 'Agent guide', slug: 'agents' }],
				},
			],
		}),
	],
});
