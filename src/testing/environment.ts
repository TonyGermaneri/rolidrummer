import { builtinEnvironments, type Environment } from 'vitest/runtime'

// Compile real Vue render functions while keeping execution in Node. The
// component host supplies an in-memory renderer, not a browser emulator.
export default { ...builtinEnvironments.node, name: 'component-contract', viteEnvironment: 'client' } satisfies Environment
