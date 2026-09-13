return function(Context)
    local Components = {}

    Components.Theme = {
        background = Color3.fromRGB(15, 18, 24),
        panel = Color3.fromRGB(24, 29, 38),
        raised = Color3.fromRGB(34, 41, 53),
        accent = Color3.fromRGB(88, 130, 255),
        success = Color3.fromRGB(83, 201, 139),
        warning = Color3.fromRGB(255, 190, 92),
        danger = Color3.fromRGB(239, 95, 105),
        text = Color3.fromRGB(238, 241, 247),
        muted = Color3.fromRGB(158, 168, 186),
    }

    local Theme = Components.Theme

    local function corner(instance, radius)
        local value = Instance.new("UICorner")
        value.CornerRadius = UDim.new(0, radius or 9)
        value.Parent = instance
    end

    function Components.Page(parent)
        local page = Instance.new("ScrollingFrame")
        page.BackgroundTransparency = 1
        page.BorderSizePixel = 0
        page.Size = UDim2.fromScale(1, 1)
        page.CanvasSize = UDim2.new()
        page.AutomaticCanvasSize = Enum.AutomaticSize.Y
        page.ScrollBarThickness = 5
        page.ScrollBarImageColor3 = Theme.accent
        page.ScrollingDirection = Enum.ScrollingDirection.Y
        page.Visible = false
        page.Parent = parent
        local padding = Instance.new("UIPadding")
        padding.PaddingLeft = UDim.new(0, 10)
        padding.PaddingRight = UDim.new(0, 10)
        padding.PaddingTop = UDim.new(0, 10)
        padding.PaddingBottom = UDim.new(0, 16)
        padding.Parent = page
        local layout = Instance.new("UIListLayout")
        layout.Padding = UDim.new(0, 9)
        layout.SortOrder = Enum.SortOrder.LayoutOrder
        layout.Parent = page
        return page
    end

    function Components.Label(parent, text, height, kind)
        local label = Instance.new("TextLabel")
        label.BackgroundTransparency = 1
        label.Size = UDim2.new(1, 0, 0, height or 28)
        label.Font = kind == "title" and Enum.Font.GothamBold or (kind == "mono" and Enum.Font.Code or Enum.Font.Gotham)
        label.Text = text or ""
        label.TextColor3 = kind == "muted" and Theme.muted or Theme.text
        label.TextSize = kind == "title" and 18 or (kind == "small" and 12 or 14)
        label.TextWrapped = true
        label.TextXAlignment = Enum.TextXAlignment.Left
        label.TextYAlignment = Enum.TextYAlignment.Center
        label.Parent = parent
        return label
    end

    function Components.Section(parent, text)
        local label = Components.Label(parent, string.upper(text), 30, "title")
        label.TextSize = 14
        label.TextColor3 = Theme.accent
        return label
    end

    function Components.Button(parent, text, callback, color)
        local button = Instance.new("TextButton")
        button.AutoButtonColor = true
        button.Size = UDim2.new(1, 0, 0, 46)
        button.BackgroundColor3 = color or Theme.raised
        button.BorderSizePixel = 0
        button.Font = Enum.Font.GothamBold
        button.Text = text
        button.TextColor3 = Theme.text
        button.TextSize = 14
        button.Parent = parent
        corner(button)
        if callback then
            button.Activated:Connect(function()
                local ok, err = pcall(callback, button)
                if not ok then Context.Logger:Error("UI action failed: " .. tostring(err)) end
            end)
        end
        return button
    end

    function Components.Toggle(parent, text, initial, callback)
        local enabled = initial == true
        local button
        local function render()
            button.Text = (enabled and "✓  " or "○  ") .. text
            button.BackgroundColor3 = enabled and Color3.fromRGB(48, 82, 76) or Theme.raised
            button.TextColor3 = enabled and Theme.success or Theme.text
        end
        button = Components.Button(parent, "", function()
            enabled = not enabled
            render()
            if callback then callback(enabled) end
        end)
        render()
        return {
            Instance = button,
            Get = function() return enabled end,
            Set = function(_, value, silent)
                enabled = value == true
                render()
                if not silent and callback then callback(enabled) end
            end,
        }
    end

    function Components.NumberInput(parent, labelText, initial, callback)
        local frame = Instance.new("Frame")
        frame.Size = UDim2.new(1, 0, 0, 62)
        frame.BackgroundColor3 = Theme.panel
        frame.BorderSizePixel = 0
        frame.Parent = parent
        corner(frame)
        local label = Components.Label(frame, labelText, 22, "small")
        label.Position = UDim2.new(0, 12, 0, 4)
        label.Size = UDim2.new(0.52, -12, 0, 22)
        local box = Instance.new("TextBox")
        box.AnchorPoint = Vector2.new(1, 0.5)
        box.Position = UDim2.new(1, -8, 0.5, 0)
        box.Size = UDim2.new(0.46, 0, 0, 44)
        box.BackgroundColor3 = Theme.raised
        box.BorderSizePixel = 0
        box.ClearTextOnFocus = false
        box.Font = Enum.Font.Code
        box.PlaceholderText = "number"
        box.Text = initial ~= nil and tostring(initial) or ""
        box.TextColor3 = Theme.text
        box.PlaceholderColor3 = Theme.muted
        box.TextSize = 17
        box.Parent = frame
        corner(box, 8)
        local lastValid = box.Text
        box.FocusLost:Connect(function()
            local number = tonumber(box.Text)
            if not number then
                box.Text = lastValid
                Context.Logger:Warn(labelText .. ": invalid number")
                return
            end
            local ok, err = callback(number)
            if ok == false then
                box.Text = lastValid
                Context.Logger:Warn(labelText .. ": " .. tostring(err))
            else
                lastValid = tostring(number)
                box.Text = lastValid
            end
        end)
        return box
    end

    function Components.Dropdown(parent, labelText, options, selected, callback)
        local object = { Options = {}, Selected = selected }
        local frame = Instance.new("Frame")
        frame.Size = UDim2.new(1, 0, 0, 64)
        frame.BackgroundColor3 = Theme.panel
        frame.BorderSizePixel = 0
        frame.ClipsDescendants = true
        frame.Parent = parent
        corner(frame)
        local label = Components.Label(frame, labelText, 20, "small")
        label.Position = UDim2.new(0, 12, 0, 4)
        label.Size = UDim2.new(1, -24, 0, 20)
        local main = Instance.new("TextButton")
        main.Position = UDim2.new(0, 8, 0, 25)
        main.Size = UDim2.new(1, -16, 0, 34)
        main.BackgroundColor3 = Theme.raised
        main.BorderSizePixel = 0
        main.Font = Enum.Font.GothamBold
        main.TextColor3 = Theme.text
        main.TextSize = 13
        main.Parent = frame
        corner(main, 7)
        local list = Instance.new("ScrollingFrame")
        list.Position = UDim2.new(0, 8, 0, 64)
        list.Size = UDim2.new(1, -16, 0, 0)
        list.BackgroundTransparency = 1
        list.BorderSizePixel = 0
        list.ScrollBarThickness = 4
        list.AutomaticCanvasSize = Enum.AutomaticSize.Y
        list.CanvasSize = UDim2.new()
        list.Parent = frame
        local layout = Instance.new("UIListLayout")
        layout.Padding = UDim.new(0, 4)
        layout.Parent = list
        local open = false

        local function render()
            main.Text = tostring(object.Selected or "-") .. "  ▼"
        end
        local function close()
            open = false
            frame.Size = UDim2.new(1, 0, 0, 64)
            list.Size = UDim2.new(1, -16, 0, 0)
        end
        local function rebuild(newOptions, newSelected)
            object.Options = newOptions or {}
            if newSelected ~= nil then object.Selected = newSelected end
            for _, child in ipairs(list:GetChildren()) do
                if child:IsA("GuiButton") then child:Destroy() end
            end
            for _, option in ipairs(object.Options) do
                local value = option
                local choice = Components.Button(list, tostring(value), function()
                    object.Selected = value
                    render()
                    close()
                    if callback then callback(value) end
                end, Theme.raised)
                choice.Size = UDim2.new(1, -5, 0, 38)
                choice.TextSize = 12
            end
            render()
            if open then
                local height = math.min(190, #object.Options * 42)
                list.Size = UDim2.new(1, -16, 0, height)
                frame.Size = UDim2.new(1, 0, 0, 68 + height)
            end
        end
        main.Activated:Connect(function()
            open = not open
            if open then
                local height = math.min(190, #object.Options * 42)
                list.Size = UDim2.new(1, -16, 0, height)
                frame.Size = UDim2.new(1, 0, 0, 68 + height)
            else close() end
        end)
        object.Instance = frame
        object.SetOptions = function(_, newOptions, newSelected) rebuild(newOptions, newSelected) end
        object.Get = function() return object.Selected end
        rebuild(options or {}, selected)
        return object
    end

    function Components.Status(parent, labelText, initial)
        local label = Components.Label(parent, labelText .. ": " .. tostring(initial or "-"), 30, "mono")
        label.TextSize = 13
        return {
            Instance = label,
            Set = function(_, value, color)
                label.Text = labelText .. ": " .. tostring(value or "-")
                label.TextColor3 = color or Theme.text
            end,
        }
    end

    return Components
end
