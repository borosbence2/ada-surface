import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

import type { KernelStatus } from '../shared/types';

/**
 * A full-resolution tessellation of a large scene is many megabytes of base64
 * in one message; the SDK's 10 MB default would close the connection on it.
 */
const kMaxMessageBytes = 256 * 1024 * 1024;

/** How much of the kernel's stderr to keep for the "kernel stopped" message. */
const kStderrTailBytes = 4000;

/**
 * The ada-mcp sidecar: started with --ui-tools, spoken to over MCP stdio with
 * the official client SDK. Studio is an MCP client like any agent, which also
 * proves on every run that the kernel's server interoperates with a real
 * client implementation.
 */
export class Kernel {
  private client: Client | null = null;
  private transport: StdioClientTransport | null = null;
  private stderrTail = '';

  constructor(
    private readonly binary: string,
    private readonly clientVersion: string,
    private readonly onStatus: (status: KernelStatus) => void,
  ) {}

  async start(): Promise<void> {
    await this.stop();
    this.onStatus({ state: 'starting' });
    this.stderrTail = '';

    const transport = new StdioClientTransport({
      command: this.binary,
      args: ['--ui-tools'],
      stderr: 'pipe',
      maxBufferSize: kMaxMessageBytes,
    });
    transport.stderr?.on('data', (chunk: Buffer) => {
      this.stderrTail = (this.stderrTail + chunk.toString()).slice(-kStderrTailBytes);
    });
    transport.onclose = () => {
      if (this.transport !== transport) {
        return; // a deliberate stop or restart, already reported
      }
      this.client = null;
      this.transport = null;
      const detail = this.stderrTail.trim();
      this.onStatus({
        state: 'stopped',
        message: detail ? `kernel exited: ${detail}` : 'kernel exited',
      });
    };

    const client = new Client({ name: 'ada-studio', version: this.clientVersion });
    try {
      await client.connect(transport);
    } catch (error) {
      this.onStatus({
        state: 'stopped',
        message: `could not start ${this.binary}: ${String(error)}`,
      });
      throw error;
    }
    this.client = client;
    this.transport = transport;
    this.onStatus({
      state: 'ready',
      version: client.getServerVersion()?.version ?? 'unknown',
      binary: this.binary,
    });
  }

  async stop(): Promise<void> {
    const client = this.client;
    this.client = null;
    this.transport = null;
    await client?.close();
  }

  /**
   * Calls a tool and returns its structured result. A tool-level error
   * (isError) becomes a rejected promise carrying the kernel's message.
   */
  async call(name: string, args: Record<string, unknown> = {}): Promise<Record<string, unknown>> {
    if (!this.client) {
      throw new Error('the kernel is not running');
    }
    const result = await this.client.callTool({ name, arguments: args });
    const content = (result.content ?? []) as { type: string; text?: string }[];
    const text = content
      .filter((c) => c.type === 'text')
      .map((c) => c.text ?? '')
      .join('\n');
    if (result.isError) {
      throw new Error(text || `${name} failed`);
    }
    if (!result.structuredContent) {
      throw new Error(`${name} returned no structured content`);
    }
    return result.structuredContent as Record<string, unknown>;
  }
}
