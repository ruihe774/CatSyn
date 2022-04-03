return function (template, tenv, varctx)
    local res = template
    aegisub.debug.out(5, "Running text template '%s'\n", res)

    -- Replace the variables in the string (this is probably faster than using a custom function, but doesn't provide error reporting)
    if varctx then
        aegisub.debug.out(5, "Has varctx, replacing variables\n")
        local function var_replacer(varname)
            varname = string.lower(varname)
            aegisub.debug.out(5, "Found variable named '%s', ", varname)
            if varctx[varname] ~= nil then
                aegisub.debug.out(5, "it exists, value is '%s'\n", varctx[varname])
                return varctx[varname]
            else
                aegisub.debug.out(5, "doesn't exist\n")
                aegisub.debug.out(2, "Unknown variable name: %s\nIn karaoke template: %s\n\n", varname, template)
                return "$" .. varname
            end
        end
        res = string.gsub(res, "$([%a_]+)", var_replacer)
        aegisub.debug.out(5, "Done replacing variables, new template string is '%s'\n", res)
    end

    -- Function for evaluating expressions
    local function expression_evaluator(expression)
        f, err = loadstring(string.format("return (%s)", expression))
        if (err) ~= nil then
            aegisub.debug.out(2, "Error parsing expression: %s\nExpression producing error: %s\nTemplate with expression: %s\n\n", err, expression, template)
            aegisub.cancel()
        else
            setfenv(f, tenv)
            local res, val = pcall(f)
            if res then
                return tostring(val)
            else
                aegisub.debug.out(2, "Runtime error in template expression: %s\nExpression producing error: %s\nTemplate with expression: %s\n\n", val, expression, template)
                aegisub.cancel()
            end
        end
    end
    -- Find and evaluate expressions
    aegisub.debug.out(5, "Now evaluating expressions\n")
    res = string.gsub(res , "!(.-)!", expression_evaluator)
    aegisub.debug.out(5, "After evaluation: %s\nDone handling template\n\n", res)

    return res
end
