#!/bin/python3
import numpy as np
import plotly.graph_objects as go

# -----------------------------
# Grid
# -----------------------------
x = np.linspace(-50, 50, 101)
y = np.linspace(-50, 50, 101)
X, Y = np.meshgrid(x, y)

# Surface: z = x*y
Z = X * Y

# -----------------------------
# Tangency point
# -----------------------------
x0, y0 = 10, 10
z0 = x0 * y0

# Tangent plane
# df/dx = y, df/dy = x
T = -z0 + y0 * X + x0 * Y

# -----------------------------
# Plot
# -----------------------------
fig = go.Figure()

# Tangent plane (opaque so it reads clearly against the saddle)
fig.add_trace(
    go.Surface(
        x=X, y=Y, z=T,
        colorscale=[[0, "darkred"], [1, "darkred"]],
        showscale=False,
        lighting=dict(diffuse=0.0, ambient=1.0),
        hoverinfo="skip",
    )
)

# Saddle
fig.add_trace(
    go.Surface(
        x=X, y=Y, z=Z,
        colorscale="Viridis",
        showscale=False,
        contours=dict(
            x=dict(show=True, color="black", width=1, start=-50, end=50, size=2, highlight=False),
            y=dict(show=True, color="black", width=1, start=-50, end=50, size=2, highlight=False),
            z=dict(highlight=False),
        ),
        hoverinfo="skip",
    )
)

axis_settings = dict(
    visible=False,
    showbackground=False,
    showgrid=False,
    zeroline=False,
)

fig.update_layout(
    scene=dict(
        xaxis=axis_settings,
        yaxis=axis_settings,
        zaxis=axis_settings,
        aspectmode="manual",
        aspectratio=dict(x=1, y=1, z=0.8),
        camera=dict(
            eye=dict(x=0.7801, y=-1.4871, z=0.35),
        ),
        bgcolor="rgba(0,0,0,0)",
    ),
    width=550,
    height=550,
    margin=dict(l=0, r=0, t=0, b=0),
    paper_bgcolor="rgba(0,0,0,0)",
    plot_bgcolor="rgba(0,0,0,0)",
)

fig.write_image("tangent_plane.pdf")
