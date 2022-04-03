local Transform, prototype, metatable

local wrt_center = function (m, center)
    if center == nil then
        return m
    else
        local tm1 = Transform.translation({-center[1], -center[2]})
        local tm2 = Transform.translation(center)
        return tm1 * m * tm2
    end
end

Transform = {
    new = function (m11, m12, m21, m22, dx, dy)
        local m = {m11, m12, m21, m22, dx, dy}
        return setmetatable(m, metatable)
    end;

    identity = function ()
        return Transform.new(1, 0, 0, 1, 0, 0)
    end;

    translation = function (distance)
        return Transform.new(1, 0, 0, 1, distance[1], distance[2])
    end;

    rotation = function (angle, center)
        local radian = angle / 180 * math.pi
        local rm = Transform.new(math.cos(radian), math.sin(radian), -math.sin(radian), math.cos(radian), 0, 0)
        return wrt_center(rm, center)
    end;

    scale = function (scale, center)
        local sm = Transform.new(scale, 0, 0, scale, 0, 0)
        return wrt_center(sm, center)
    end;

    skew = function (angle_x, angle_y, center)
        local radian_x = angle_x / 180 * math.pi
        local radian_y = angle_y / 180 * math.pi
        local sm = Transform.new(1, math.tan(radian_y), math.tan(radian_x), 1, 0, 0)
        return wrt_center(sm, center)
    end;

    multiply = function (m1, m2)
        return Transform.new(
            m1[1] * m2[1] + m1[2] * m2[3],
            m1[1] * m2[2] + m1[2] * m2[4],
            m1[3] * m2[1] + m1[4] * m2[3],
            m1[3] * m2[2] + m1[4] * m2[4],
            m1[5] * m2[1] + m1[6] * m2[3] + m2[5],
            m1[5] * m2[2] + m1[6] * m2[4] + m2[6]
        )
    end;
}

prototype = {
    copy = function (m)
        return Transform.new(m[1], m[2], m[3], m[4], m[5], m[6])
    end;

    transform_point = function (m, point)
        return {
            m[1] * point[1] + m[3] * point[2] + m[5],
            m[2] * point[1] + m[4] * point[2] + m[6]
        }
    end;

    determinant = function (m)
        return m[1] * m[4] - m[2] * m[3]
    end;

    invert = function (m)
        local det = prototype.determinant(m)
        return Transform.new(m[4]/det, -m[2]/det, -m[3]/det, m[1]/det, (m[3]*m[6]-m[4]*m[5])/det, (m[2]*m[5]-m[1]*m[6])/det)
    end;

    is_invertible = function (m)
        return prototype.determinant(m) ~= 0
    end;

    is_identity = function (m)
        return m[1] == 1 and m[2] == 0 and m[3] == 0 and m[4] == 1 and m[5] == 0 and m[6] == 0
    end
}

metatable = {
    __index = prototype;
    __mul = Transform.multiply;
}

return setmetatable(Transform, {
    __index = prototype
})
