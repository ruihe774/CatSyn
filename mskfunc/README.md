# mskfunc

A typography and shape library for Aegisub, based on DirectWrite and Direct2D.

## Features

- OpenType variable fonts support
- Text inline formatting
- Shape transformation and combination
- Seamless with karaoke template
- High speed native code

## Requirements

- Windows 10 Fall Creators Update or newer
- Latest [Aegisub Daydream Cafe Edition](https://github.com/Ristellise/AegisubDC)

These requirements are mandatory.
If you encounter a crash or weird error, it is probably because either requirement is not satisfied. 

## Compilation

Do not forget to checkout all submodules beforehand. Boost is required. Clang-CL is recommended.

```
mkdir build  & cd build
cmake -TClangCL -DBOOST_ROOT=/path/to/boost ..
cmake --build . --config=Release
```

## Installation

Put the built `mskfunc.dll` into the *root* directory of Aegisub.
(Not the `include` or `autoload` dir but the *root* dir where `Aegisub.exe` is present!)

## Usage

I assume you have basic knowledge of karaoke template in Aegisub.
In this documentation, to indicate the `Effect` of an ASS line, this notation is used: `Effect | Text`.
A line with no `Effect` means it is not a comment but a dialogue.

mskfunc can be loaded using `_G.require()`:

```
code once               |   mskfunc = _G.require("mskfunc"); mskfunc.misc.hack_karaskel(_G.karaskel)
```

The `mskfunc.misc.hack_karaskel()` call can help us eliminate explicit `tostring()` calls in template.
It is not mandatory; however, it brings us convenience.

Now we can use mskfunc for typesetting and shape transformation.
For example, draw the text:

```
code line               |   layout = mskfunc.layout.from_plaintext(line.text_stripped, line.styleref)
code line               |   shape = layout:to_shape(true)
template line notext    |   {\p1}!shape!{\p0}
                        |   我能吞下玻璃，而不伤身体。
```

The text is converted to ASS draw commands. It is placed just at the right position.
The conversion from text to shape is sometimes useful when we want to use text as mask:

```
comment                 |   \clip always uses coordinates origined at (0, 0)
comment                 |   so we need to move the shape
code line               |   shape = shape:transform(mskfunc.transform.translation({1000, 500}))
template line           |   {\clip(!shape!)}
```

Text layout supports OpenType variable fonts and inline formatting.
Shape supports transformation and combination.
For example:

```
code once               |   mskfunc.context.decimal_places = 2
code line               |   text = "我能吞下玻璃而不伤身体"
code line               |   layout = mskfunc.layout.from_plaintext(text, {fn="Source Han Sans VF", b=333, fs=80, locale="zh-CN"})
code line               |   layout:set_style({b=777, range={5, 2}}); layout:set_style({u=1, range={10, 2}})
code line               |   shape = layout:to_shape({lt=true}); metrics = layout:metrics()
code line               |   overlay = mskfunc.shape.fromstring("m 0 0 l 0 " .. metrics.height .. " l " ..  metrics.width .. " 0")
code line               |   shape = shape:combine(overlay, "^"):anchor({width=metrics.width, height=metrics.height})
template line notext    |   {\p1}!shape!{\p0}
```

![Screenshot](https://s2.loli.net/2021/12/23/YlKpT4EfZmDv5dA.png)

### Standalone

mskfunc does not depend on Aegisub. It can be used in plain moonjit runtime.
You can find a `moonjit.exe` in build outputs and mskfunc can be loaded in it.
For example, a script to convert SVG to ASS:

```lua
-- Saved as svg2assdraw.lua
local mskfunc = require("mskfunc")
local svg = io.stdin:read("*a")
local ass = mskfunc.composition.load_svg(svg):to_shape()
io.stdout:write(tostring(ass))
```

```
moonjit svg2assdraw.lua < in.svg > out.txt
```

## Reference

In mskfunc, if a function get invalid arguments, its behavior is undefined.
Aegisub is likely to crash with all work lost. So, take care of yourself.

Methods (e.g. `Shape:combine()`) can also be called in a binding-free way:
`a:combine(b, "+")` is equivalent to `mskfunc.shape.combine(a, b, "+")`.

### Layout

#### `layout.from_plaintext(text: string, base_style: table) -> TextLayout`

Create a `TextLayout` from plaintext. `base_style` is the default style applied to the text.
See `TextLayout:set_style()` for the structure of style table.

#### `TextLayout:set_style(style: table) -> nil`

Set an inline formatting to `TextLayout`. `style` is a table containing formatting overrides.
Supported keys are:

- `range`: a table of a pair of numbers. It is the text range that this inline formatting takes effect.
  The first number is the start index, counting from 1.
  The second number is the length.
  The index and length are measured in *UTF-16 code units*.
  Required in `set_style()`.
  `base_style` does not have this field.
- `fn`, `fontname`: a string specifying the font *family* name.
- `fs`, `fontsize`: a number specifying the font size.
- `dip`: a boolean. If it is set to true, the nominal size (units per em) is used to determine the font size.
  This provides a consistent size across different fonts.
  If it is set to false, the real dimension (ascender and descender) is used to determine the font size.
  This is ASS's behavior. Default is false. Can only be set in `base_style`.
- `locale`: a string specifying the locale of text. This affects multilingual fonts like Source Han.
- `b` (number), `bold` (true/false): specify the font weight. For normal fonts, values supported by the font can be used.
  For variable fonts, arbitrary values within axis range can be used.
  Two special cases: `0` means `400` (Regular), `1` means `700` (Bold).
- `i` (number), `italic` (true/false): specify whether to use italic font.
- `fvar`: a table specifying values for font variation axes. For example:
  ```
  {
    wdth = 150,    -- set width to 150
    wght = 650,    -- set weight to 650  
  }
  ```
  You can refer to the manual of a font family to see what axes are supported.
- `u` (0/1), `underline` (true/false)
- `s` (0/1), `strikeout` (true/false)
- `fsp`, `spacing`: a number specifying the spacing after each character.
- `lsp`: a number specifying the line spacing. It is a scaling factor applied to the computed line height.
  Can only be set in `base_style`.
- `dir`: a number or a table specifying the reading direction and flow direction of the text.
  If it is a single number, it specifies the reading direction and implies a flow direction.
  If it is a table of a pair of numbers, the first number specifies the reading direction and
  the second number specifies the flow direction.
  The reading direction and the flow direction must be perpendicular. 

  Reading directions:
  - `0`: left to right
  - `1`: right to left
  - `2`: top to bottom
  - `3`: bottom to top

  Flow directions:
  - `0`: top to bottom
  - `1`: bottom to top
  - `2`: left to right
  - `3`: right to left

  For example, to simulate `@` in ASS, we can use `dir = {2, 3}` for vertical writing.
  Unlike VSFilter, this is elegant vertical writing with consistent font size and correct handling for complex scripts.

  Can only be set in `base_style`.
- `lbox`: a table of a pair of numbers specifying the width and height of the layout box.
  A line longer than the width of layout box will result in wrapping or overflow.
  Can only be set in `base_style`.
- `wrap`: a string specifying the wrapping behavior:
  - `no`: no wrapping
  - `word`: wrapping. Do not break word.
  - `break`: wrapping. Can break word.
  - `char`: wrapping. Every character can break.

  Can only be set in `base_style`.
- `an`, `align`: a number specifying the alignment.
  `1`, `4`, `7` for leading align, `2`, `5`, `8` for center align, `3`, `6`, `9` for trailing align.
  Can only be set in `base_style`.
- `align`: a string specifying the alignment: `leading`, `trailing`, `center`, or `justified`.
  Can only be set in `base_style`.
- `kern` (0/1): a number specifying whether to enable font kerning.
- `feat`: a table specifying the OpenType features to enable. For example:
  ```
  {
    liga = 1,    -- enable ligature
    ss01 = 1,    -- enable stylic set 1
  }
  ```
  You can refer to the manual of a font family to see what features are supported.

#### `TextLayout:to_shape(anchor: bool | table = false) -> Shape`

Draw `TextLayout` to `Shape`.

`anchor` specifies whether to anchor the shape according to metrics.
If set to true, it is equivalent to:

```lua
layout:to_shape():anchor(layout:metrics())
```

If anchored, the shape, if used in template, will be put at the original position of the line without the help of `\pos`.
See `Shape:anchor()` for more information.

`anchor` also accepts a table:
- `lt=true`: anchor left-top (move the text box to (0, 0))
- `rb=true`: anchor right-bottom (stretch the text box to the width and height of metrics)

#### `TextLayout:metrics() -> table`

Get the metrics of `TextLayout`:
- `left`: the left boundary of the text.
- `top`: the top boundary of the text.
- `width`: the width of the text.
- `height`: the height of the text.
- `line_count`: the number of lines of the text.

### Shape

A shape consists of paths. "Path" is my own expression for a draw command along with its points.
For example, `m 0 0 l 100 100 200 100 b 200 200 300 100 200 0` is a shape.
`l 100 100 200 100` is its second path. `l` is the command. `{100, 100}` and `{200, 100}` are the points.
My expression may differ from other's expression. Keep that in mind.

A special command `e` means closing without filling.
(it is not supported by ASS -- ASS will always fill.)
This is for later stroking.

#### `shape.new(data: table) -> Shape`

Create a `Shape`. For example:

```lua
shape = mskfunc.shape.new({{"m", {{0, 0}}}, {"l", {{100, 100}, {200, 100}}}, {"b", {{200, 200}, {300, 100}, {200, 0}}}})
```

#### `shape.fromstring(cmd: string) -> Shape`

Create a `Shape` from a string of ASS draw commands. For example:

```lua
shape = mskfunc.shape.fromstring("m 0 0 l 100 100 200 100 b 200 200 300 100 200 0")
```

#### `Shape:transform(matrix: Transform) -> Shape`

Create a transformed shape. The transform matrix is a 3-by-2 matrix. See `transform.new()` for details.
For example:

```lua
shape = shape:transform(mskfunc.transform.rotation(30) * mskfunc.transform.translation({100, 100}))
```

The result shape is rotated by 30 degree clockwise and translated by (100, 100).

#### `Shape:subset(begin: number, end: number) -> Shape`

Create a subset shape consisting of the paths from `begin` to `end` (inclusive, counting from 1).
For example:

```lua
shape = mskfunc.shape.fromstring("m 0 0 l 100 100 200 100 m 300 300 b 200 200 300 100 200 0 m 500 500 l 500 0 l 0 0")
subshape = shape:subset(3, 4)
tostring(subshape)
-- "m 300 300 b 200 200 300 100 200 0"
```

#### `Shape:get_path(i: number, points: boolean = true) -> string | string, table`

Get i-th path in the shape. If `points` is false, only the command char is returned.
For example:

```lua
cmd, points = shape:get_path(2)
cmd
-- "l"
points
-- {{100, 100}, {200, 100}}
```

#### `Shape:set_path(i: number, command: string | nil, points: table | nil = nil) -> nil`

Set i-th path in the shape.
If `command` is nil, the original command is preserved.
If `points` is nil, the original points are preserved.
If `i` is equal to `#shape + 1`, it appends.
For example:

```lua
shape:set_path(2, "b", {{100, 100}, {50, 100}, {150, 100}})
```

#### `Shape:stream(f: function) -> Shape`

Create a new shape with each command and point transformed by the function `f`.
The signature of `f` should be:
`f(point: Point | nil, command: string | nil) -> Point | string | false | nil`.
(`Point` is a table of a pair of numbers, i.e. `{number, number}`.)
For each path in the shape, `f` will first accept `(nil, command)`.
In this call, if `f` returns a string, the original command will be replaced by this new command.
If `f` returns nil, the original command is preserved.
If `f` returns false, the whole path is dropped and no points in this path will be passed to `f`.
After this call, `f` will subsequently accept `(point, nil)` for each point in this path.
`point` is a table of a pair of numbers.
If `f` returns a table, the original point will be replaced by this new point.
If `f` returns nil, the original point is preserved.
If `f` returns false, this point is dropped.
For example:

```lua
-- translate shape by (100, 200)
shape = shape:stream(function(point, cmd) if point then return {point[1] + 100, point[2] + 200} end end)
-- In real case, you should always use Shape:transform() for such translation -- it's faster
```

#### `Shape:copy() -> Shape`

Create a copy of the shape.

#### `Shape:move_to(point: Point) -> nil`, `Shape:lines_to(points: table) -> nil`, `Shape:beziers_to(points: table) -> nil`, & `Shape:close_path() -> nil`

Add a path to the shape.

#### `Shape:anchor(box: table) -> Shape`

Anchor the shape to a box. `box` is a table:

- `left`: left boundary
- `top`: top boundary
- `width`: the width of the box
- `height`: the height of the box

In ASS, the boundary of a draw is detected and used in positioning and collision detection.
For example, in an `\an2` draw,
the height of the draw is used in determining how far it will be positioned from the bottom edge of the video,
and the width is used to make it centered.
For the shape generated from a text layout,
it is likely that the left-top is not at (0, 0) and the boundary of the text does not fully stretch the metrics box.
This can result in incorrect positioning:

```
code line               |   layout = mskfunc.layout.from_plaintext("I can eat glass. I won't hurt.", {fn="Segoe UI"})
code line               |   shape = layout:to_shape()
template line notext    |   !maxloop(2)!{\an2\p1}!shape!{\p0}
```

![Screenshot](https://s2.loli.net/2021/12/23/Ia7EenK5zfRHwh1.png)

To fix it, we can anchor the shape to the metrics box:

```
code line               |   shape = shape:anchor(layout:metrics())
template line notext    |   !maxloop(2)!{\an2\p1}!shape!{\p0}
```

![Screenshot](https://s2.loli.net/2021/12/23/4vIygZ75zj2bu63.png)

The shape is now at the correct position. No `\pos` is used.

`TextLayout:to_shape()` also accepts an argument that can anchor the shape automatically:
`shape = layout:to_shape(true)`.
However, if you do transformation on the shape,
it is recommended to anchor the shape in the last step, after all transformations.

FYI, `Shape:anchor()` is equivalent to:

```lua
function anchor(shape, box)
  local anchored = shape:transform(mskfunc.transform.translation({-box.left, -box.top}))
  anchored.move_to({0, 0}); anchored.lines_to({{0,0}})
  anchored.move_to({box.width, box.height}); anchored.lines_to({{box.width, box.height}})
  return anchored
end
```

#### `Shape:bounds() -> table | nil`

Get the bounds of the shape. Return a table of: `left`, `top`, `width`, `height`.
Return nil if the shape is empty.

#### `Shape:minmax() -> table | nil`

Get the minimal x, y and maximal x, y in the shape. Return a table of: `left`, `top`, `width`, `height`.
Return nil if the shape is empty.

#### `Shape:combine(shape2: Shape, mode: string = "+") -> Shape`

Create a new shape that is the combination of two shapes.
`mode` specifies the combination mode:

- `+`, `union`: the new shape is the union of both.
- `*`, `intersect`: the new shape is the intersection.
- `^`, `xor`: the new shape is the area
  that exists in the first shape but not the second and the area that exists in the second shape but not the first. 
- `-`, `exclude`: the new shape is the area that exists in the first shape but not the second.

![Demo](https://docs.microsoft.com/en-us/windows/win32/api/d2d1/images/geometry_combine_modes.png)

#### `Shape:compare(shape2: Shape) -> string`

Compare two shapes and compute their relation:

- `disjoint`: these two shapes are disjoint.
- `is_contained`: shape is contained in shape2.
- `contains`: shape contains shape2.
- `overlap`: these two shapes overlap but neither completely contains the other.

#### `Shape:contains(point: Point) -> boolean`

Detect whether the shape contains a point.

#### `Shape:compute_area() -> number`

Compute the area of a shape.

#### `Shape:compute_length() -> number`

Compute the length of a shape.

#### `Shape:point_at_length(length: number, i: number = 1) -> Point, Point, number, number, number`

Compute the point at the specified distance along the shape, beginning walking from i-th path.
Return the point, the unit tangent vector at the point,
the index of the path at which the point resides,
the index of the figure (a figure is paths beginning with `m`, ending with optional `c`, followed by next `m`)
at which the point resides,
and the length from the start of the shape to the start of the path at which the point resides.

#### `Shape:flatten() -> Shape`

Create a flattened shape that only contains line segments but no beziers.

#### `Shape:outline() -> Shape`

Create a shape that is the outline of the given shape.
i.e. equivalent fill with no transverse intersections.

#### `Shape:widen(stroke: number | table) -> Shape`

Create a stroked shape. If `stroke` is a number, it specifies the stroke width.
If `stroke` is a table, it specifies the stroke style:

- `width` (number): the stroke width.
- `outline` (boolean): stroke only the outline of the shape (true), or stroke all paths (false).
- `cap`, `start_cap`, `end_cap`, `dash_cap`: specify the line cap: `"flat"`, `"square"`, `"round"`, or `"triangle"`.
- `line_join`: specify the line join: `"miter"`, `"bevel"`, `"round"`.
  `"+"` means using miter in miter limit, using bevel when beyond miter limit.
- `miter_limit`: the limit of the thickness of the join on a mitered corner.
- `dash`: a string or a table.
  If it is a string, it specifies a predefined dash pattern:
  - `"-"`: dash
  - `"."`: dot
  - `"-."`: dash-dot
  - `"-.."`: dash-dot-dot

  If it is a table, it specifies a custom dash pattern.
  For example, `{2, 1, 3, 1}` means one 2 unit long dash followed by 1 unit long space.
  Then one 3 unit long dash followed by 1 unit long space.
  This pattern is repeated to draw a dashed line.
- `dash_offset` (number): the offset of dash.
  A positive value shifts the dash pattern toward the start of the stroked shape.
  A negative value shifts toward the end.

For example:

```lua
layout = mskfunc.layout.from_plaintext("我能吞下玻璃而不伤身体。", {fn="Source Han Sans VF", locale="zh-CN", b=900, fs=100, fsp=5})
shape = layout:to_shape(); metrics = shape:metrics()
shape = shape:widen({width=2,outline=true,dash={5,1}}); shape = shape:anchor(metrics)
```

![Screenshot](https://s2.loli.net/2021/12/23/yqcMKB1utRQkCs6.png)

#### `_G.tostring(shape)`

Convert a shape to a string of ASS draw commands.
If you do not call `mskfunc.misc.hack_karaskel()` at the beginning, or you want to cache the ASS draw string,
you can explicitly convert a shape to string:

```
code line               |   draw_str = _G.tostring(shape)
template line notext    |   {\p1}!draw_str!{\p0}
```

#### `shape1 .. shape2`

Concat two shapes.

### Transform

The transform library is written in Lua.
So, please avoid heavy calculations or cache the result matrix.

The transform matrix is a 3-by-2 matrix (or 3-by-3 in calculations):

![Transform Matrix](https://s2.loli.net/2021/12/23/5MpnYXdWFtKabDE.png)

The transform of a point is done in this way:

![Transform](https://s2.loli.net/2021/12/23/ZfmBxGUzvoIs1lL.png)

Transforms can be chained using matrix multiplication.
For example: `mskfunc.transform.rotation(30) * mskfunc.transform.translation({100, 100})`

A transform matrix is a Lua table storing `m11`, `m12`, `m21`, `m22`, `dx`, and `dy` in sequent order.

#### `transform.new(m11: number, m12: number, m21: number, m22: number, dx: number, dy: number) -> Transform`

Create a transform matrix.

#### `transform.identity() -> Transform`

Create an identity matrix.

#### `transform.translation(distance: Point) -> Transform`

Create a translation matrix. `distance` is a pair of numbers specifying dx and dy.

#### `transform.rotation(angle: number, center: Point = {0, 0}) -> Transform`

Create a rotation matrix. `angle` is the rotation angle *in degree*.
Positive `angle` rotates clockwise. Negative `angle` rotates counterclockwise.
`center` is a pair of numbers specifying the rotation center.

#### `transform.scale(scale: number, center: Point = {0, 0}) -> Transform`

Create a scaling matrix. `scale` is the scaling factor.
`center` is a pair of numbers specifying the scaling center.

#### `transform.skew(angle_x: number, angle_y: number, center: Point = {0, 0}) -> Transform`

Create a skew matrix. `angle_x` is the skew along x-axis. `angle_y` is the skew along y-axis.
Both angles are *in degree*.
`center` is a pair of numbers specifying the skew center.

#### `matrix1 * matrix2`, `transform.multiply(matrix1: Transform, matrix2: Transform) -> Transform`

Multiply two transform matrices (chaining two transforms).

#### `Transform:copy() -> Transform`

Create a copy of the transform.

#### `Transform:transform_point(point: Point) -> Point`

Transform a point.

Note: in most cases, what you want is `Shape:transform()`,
which is implemented in native code and is faster than manually transforming each point in a shape.

#### `Transform:determinant() -> number`

Calculate the determinant of the transform matrix.

#### `Transform:invert() -> Transform`

Calculate the inverse of the transform matrix.
Inverting a singular matrix will result in error.
You may first check `Transform:is_invertible()`.

#### `Transform:is_invertible() -> boolean`

Check whether the matrix is invertible.

#### `Transform:is_identity() -> boolean`

Check whether the matrix is an identity matrix.

### Composition

The composition lib provides methods to compose multi-layer drawing.

#### `composition.new(shape: Shape, context: Context, parent: Composition | nil = nil) -> Composition`

Create a composition.

`context` is a table of following keys:

- `anchor`: a table of `left`, `top`, `width`, and `height`, specifying the anchor box.
- `transform`: the transform to be applied to `shape`.
- `fill`: a `Color` instance specifying the fill color.
- `stroke`: a `Color` instance specifying the stroke color.
- `stroke_style`: a stroke style table.
- `mask`: a composition to be used as the clip mask of the new composition.

`parent` can be a composition. It will be drawn before (behind) this new composition.
In this way we can construct a composition chain.

A pseudo structure of `Composition`:

```
Composition {
  shape: Shape
  context: Context {
    anchor: Box
    transform: Transform
    fill: Color | nil
    stroke: Color | nil
    stroke_style: StrokeStyle
    mask: Composition | nil
  }
  parent: Composition | nil
}
```

An example:

```lua
viewport = {left=-20, top=-20, width=400, height=400}
transform = mskfunc.transform.scale(10)
comp = mskfunc.composition.new(mskfunc.shape.fromstring("m 10 5 l 0 24 l 20 21"), {anchor=viewport, transform=transform, fill={r=255, g=0, b=0, a=0}, stroke={r=0, g=255, b=0, a=0}, stroke_style={width=2, line_join="round"}})
comp = mskfunc.composition.new(mskfunc.shape.fromstring("m 15 18 l 26 18 l 28 34 l 13 31"), {anchor=viewport, transform=transform, fill={r=255, g=255, b=0}}, comp)
comp = mskfunc.composition.new(mskfunc.shape.fromstring("m 23 6 b 28 6 32 10 32 15 b 32 20 28 24 23 24 b 18 24 14 20 14 15 b 14 10 18 6 23 6"), {anchor=viewport, transform=transform, stroke={r=0, g=0, b=255, a=64}, stroke_style={width=5}}, comp)
```

```
code line               |   lines = comp:to_lines({line.x, line.y}, line.styleref.align)
template line notext    |   !maxloop(#lines)!!lines[j]!
```

![Screenshot](https://s2.loli.net/2022/01/08/Bs3T7ze4UF2YKD9.png)

#### `composition.load_svg(s: string) -> Composition`

Load an SVG document as composition. If `s` starts with `"<"`, it is treated as the content of an SVG document.
Otherwise, it is treated as a filename from which the SVG document is loaded.

An example:

```xml
<?xml version="1.0" standalone="no"?>
<!--Saved as test.svg-->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="-2 -2 40 40" width="400">
    <polygon points="10,5 0,24 20,21" fill="#FF0000" stroke="#00FF00" stroke-width="2" stroke-linejoin="round"/>
    <polygon points="15,18 26,18 28,34 13,31" fill="#FFFF00"/>
    <circle cx="23" cy="15" r="9" fill="none" stroke="#0000FF" stroke-width="5" stroke-opacity=".75"/>
</svg>
```

```
code once               |   comp = mskfunc.composition.load_svg("path\\to\\test.svg")
code line               |   lines = comp:to_lines({line.x, line.y}, line.styleref.align)
template line notext    |   !maxloop(#lines)!!lines[j]!
```

The result is the same as the previous example.

Only a very limited subset of SVG is supported.
Unsupported SVG elements (and their descendants) and attributes are ignored.
Supported elements and their attributes (excluding presentation attributes):

| Elements     | Attributes                                          |
|--------------|-----------------------------------------------------|
| `<svg>`      | `viewBox`, `width`, `height`, `preserveAspectRatio` |
| `<g>`        |                                                     |
| `<path>`     | `d`                                                 |
| `<circle>`   | `cx`, `cy`, `r`                                     |
| `<ellipse>`  | `cx`, `cy`, `rx`, `ry`                              |
| `<line>`     | `x1`, `y1`, `x2`, `y2`                              |
| `<polyline>` | `points`                                            |
| `<polygon>`  | `points`                                            |
| `<rect>`     | `x`, `y`, `width`, `height`, `rx`, `ry`             |

Supported presentation attributes:

- `transform`
- `fill` (^)
- `fill-opacity`: (*)
- `stroke`: (^)
- `stroke-opacity`: (*)
- `stroke-width`
- `stroke-linecap`
- `stroke-linejoin`
- `stroke-miterlimit`
- `stroke-dasharray` (*)
- `stroke-dashoffset` (*)
- `display`
- `visibility`

^: `rgba()` is not supported.<br>
*: percentage values are not supported.

Values with units (e.g. `em`, `pt`, etc.) are not supported.

#### `Composition:get_shape() -> Shape`

Return the shape (untransformed) of the composition.

#### `Composition:get_context() -> Context`

Return the context of the composition.

#### `Composition:get_parent() -> Composition | nil`

Return the parent of the composition, if exists.

#### `Composition:to_shape() -> Shape`

Render the composition to a shape, all layers merged. Color information is dropped.

#### `Composition:to_lines(pos: Point, align: number) -> Line{}`

Render the composition to lines.
Return a table of Line objects.

#### `Composition:stream(f: function) -> Composition`

Call `f` with each composition from back to front.
If `f` returns nil, the original composition is preserved or a new composition is created as-is,
except the `parent` field may change if the chain divergences.
If `f` returns false, this composition is dropped.
If `f` returns a new composition, the original composition is replaced.

#### `composition.line.new(draw: Shape, clip: Shape | nil, color: Color, pos: Point) -> Line`

Create a line.
Lines are one rendering target of composition.
A line corresponds to the text of one ASS event.

The pseudo structure of `Line`:

```
Line {
  draw: Shape
  clip: Shape
  color: Color
  pos: Point
}
```

A line is a table. Fields can be directly accessed. 

#### `_G.tostring(line)`, `composition.line.tostring(line: Line) -> string`

Convert line to ASS text.

#### `composition.color.new(a: number, b: number, g: number, r: number) -> Color`, `composition.color.new(a: number) -> Color`

Create a color. `a`, `b`, `g`, `r` are values for the alpha, blue, green, red channels, respectively.
These values are in the range of 0-255.
Note that alpha is not opacity but the transparency.

If only `a` is given, the current color (i.e. the color specified in ASS style) is used.

The returned color is a table of four entities: `a`, `b`, `g`, and `r`.

#### `composition.color.from_hex(hex: string) -> Color`

Parse hex color string in the format of `BBGGRR` or `AABBGGRR`, with optional prefix `&H` and suffix `&`.

#### `Color:to_hex_bgr() -> string`

Convert color to a string of the format of `BBGGRR`.

#### `Color:to_hex_alpha() -> string`

Convert the alpha of the color to a string of the format of `AA`.

### Misc

#### `misc.hack_karaskel(_G.karaskel) -> nil`

Patch karaskel to add implicit `tostring()` call in template substitution.
The original karaskel can only accept strings or numbers in template substitution.
After patched, it can accept all objects that implement `__tostring` metamethod.
`Shape` implements `__tostring` and now can be directly used in template without explicit `tostring()`.

The patch is made to `run_text_template()`:

```diff
@@ -682,7 +682,7 @@ function run_text_template(template, tenv, varctx)
                        setfenv(f, tenv)
                        local res, val = pcall(f)
                        if res then
-                               return val
+                               return tostring(val)
                        else
                                aegisub.debug.out(2, "Runtime error in template expression: %s\nExpression producing error: %s\nTemplate with expression: %s\n\n", val, expression, template)
                                aegisub.cancel()
```

#### `misc.utf8_to_utf16(utf8_str: string) -> string`

Convert UTF-8 string to UTF-16 string.
The result UTF-16 string is likely to contain bytes of zero.

#### `misc.utf16_to_utf8(utf16_str: string) -> string`

Convert UTF-16 string to UTF-8 string.

Note: all APIs in mskfunc except this function accept UTF-8 string as arguments,
despite the fact that they are internally converted to and handled in UTF-16.
Some APIs use indexes measured in UTF-16 code units.
Please distinguish the concepts of bytes, code units, and code points.

#### `misc.load_svg_as_shape(svg_doc: string) -> Shape`

Load a SVG document as `Shape`.

*Deprecated*. Use `composition.load_svg()` instead.

### Context

The context library controls the behaviors of functions in mskfunc.
We can set the context fields to change their behaviors.
For example, change the decimal places preserved when converting shape to string:

```
mskfunc.context.decimal_places = 2
```

Then when converting shape to string, point coordinates are rounded to two decimal places.
It is more precious than the default value `0`.

#### `context.decimal_places: number = 0`

The decimal places to round to when converting shape to string.
Higher value for higher precision but bigger ASS file size.
Note that all internal storage and calculations of `Shape` are in precious 32-bit floating point numbers.
This context field only takes effect when converting shape to string,
which is the only rounding step in shape manipulation.

#### `context.flattening_tolerance: number = 0`

The maximum error allowed when constructing a polygonal approximation of a shape.
Some functions implicitly flatten (part of) the shape before performing calculations.
`Shape:flatten()` explicitly flatten the whole shape.
Default value is `0`, which means the default flattening tolerance of Direct2D is used.

## Changelog

- 1.2.4

  API changes:

  - Add `Shape:minmax()`.

  Bugfix:

  - Fix compilation error on MSVC CL.
  - Fix error negative values in `tostring()` of `Shape`.

- 1.2.3

  Bugfix:

  - Remove `e` in `Composition:to_shape()`.

- 1.2.2

  API changes:

  - Add `Compositon:stream()`.
  - `clip` can now be nil when calling `composition.line.new()`.
  - Hex string, BGR integer, and sequential table can now be used where Color instances are accepted.
  - `misc.hack_karaskel()` now can be called without argument, in which case `_G.karaskel` is automatically used.

  Bugfix:

  - Fix the bug that null composition pointer could be leaked to Lua.

- 1.2.1

  API changes:

  - Add `composition.color.from_hex()`

  Other changes:

  - `TextLayout:to_shape(true)` now uses the same new anchoring behavior as `Shape:anchor()` introduced in 1.2.
  - When converting floating number to string (e.g. `tostring(shape)`),
    in the part after the decimal point, trailing zeros are trimmed.

- 1.2

  API changes:

  - Add composition lib.

  Other changes:

  - Tweak compilation options.
  - Support more SVG elements and attributes.
  - Lua code now is precompiled.
  - `Shape:anchor()`: workaround libass #582.
  - `shape.new()` with no argument now creates empty shape.

- 1.1.1

  Bugfix:

  - Fix optional arguments handling (regression in 1.1) -- make optional arguments optional.

- 1.1

  Mainly refactoring work, decoupling the implementation and the Lua interface.

  API changes:

  - Add `Shape:outline()`.
  - The default line join is changed to `"+"`.
  - Stroke style table now supports specifying all line caps with one entity of `cap`.
  - `Shape:point_at_length()` now retrieves more information.
  - Add non-filling path support -- add draw command `e`.

  Other changes:

  - Switch to a new SVG backend -- D2D1Svg. SVG support is now enabled by default.
  - Support more SVG elements and attributes.

- 1.0

  Initial release.
