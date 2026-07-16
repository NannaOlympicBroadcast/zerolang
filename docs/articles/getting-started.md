## Start With An Agent

Zerolang is designed for a human working with an agent.

The agent should author the program through the graph. The human should review
the graph summary, command output, and the `.0` projection when useful. A
projection is readable and bidirectional, but it is not the normal place for an
agent to write code.

## Install

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

Then install the agent bootstrap skill:

```sh
npx skills add vercel-labs/zerolang
```

Use the installed `zero` command in public examples. If you are developing Zero
itself, follow the repository contributor notes for checkout-local compiler
work.

## Hello World

Start by asking:

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "build hello world for zerolang"
    },
    {
      "role": "assistant",
      "text": "I’ll initialize this directory, add main, and run it."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero init",
          "output": "graph project init ok\nwrote: ./zero.toml\nwrote: ./zero.graph"
        },
        {
          "command": "zero patch --op 'addMain' --op 'addCheckWrite fn=\"main\" text=\"hello from zero\\n\"'",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run",
          "output": "hello from zero"
        }
      ]
    }
  ]
}
```

The expected projection is:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello from zero\n")
}
```

That file is a projection of `zero.graph`. Humans can read it, review it, and
occasionally edit it. Agents should normally keep using `zero query` and
`zero patch`.

## The Daily Loop

Use this loop for most tasks:

```sh
zero query
zero patch --op help
zero patch --op 'addMain'
zero check
zero test
zero run -- <args>
```

The default input is the current directory, so a package command does not need
`.` unless you want to be explicit.

## Reviewing A Projection

When a human wants to see readable text:

```sh
zero export
zero verify-projection
```

When a human intentionally edits `src/main.0`, import the projection back into
the graph before checking or running:

```sh
zero import
zero check
```

Do not use projection export as an automatic agent step. Export when a human
asks to review source-like text or when CI wants a projection drift gate.

## Build An Artifact

Use `zero build` for executable, object, or LLVM IR artifacts:

```sh
zero build --emit exe --target linux-musl-x64 --out .zero/out/app
```

For early exploration, `zero run` is usually enough.
