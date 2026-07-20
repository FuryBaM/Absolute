# `absolute.unless` syntax plugin

This is the reference implementation for Absolute syntax plugin ABI v1. It
registers the `unless` keyword and lowers

```absolute
unless (condition) statement
```

to the core form:

```absolute
if (!(condition)) statement
```

The public ABI is in `Absolute-Parser/include/plugin_api.h`. A plugin consists
of an exported `absolute_syntax_plugin_init_v1` function, a static
`AbsoluteSyntaxPluginV1` descriptor, and one or more `AbsoluteSyntaxRuleV1`
adapters. Plugins link to neither the parser nor LLVM.

Adapters receive immutable token views. On success they set `consumed_tokens`
and `replacement_source`; on failure they return zero and may set
`error_message`. Replacement storage remains owned by the plugin and must stay
valid until the adapter is invoked again.
