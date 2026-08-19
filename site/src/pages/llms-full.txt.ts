import type { APIRoute } from 'astro';
import { llmsFile } from '../lib/llms';

export const prerender = true;

export const GET: APIRoute = () => llmsFile('llms-full.txt');
