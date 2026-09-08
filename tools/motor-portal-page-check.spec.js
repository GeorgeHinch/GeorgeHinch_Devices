const { test, expect } = require("playwright/test");
const { pathToFileURL } = require("node:url");
const { resolve } = require("node:path");

test("motor firmware portal includes positioned arc setup", async ({ page }) => {
  page.on("pageerror", error => { throw error; });
  await page.setViewportSize({ width: 900, height: 900 });
  const portal = pathToFileURL(resolve("firmware/motor_controller/v1.1.35/portal.html")).href + "?preview=1";
  await page.goto(portal, { waitUntil: "load" });
  await expect(page.getByRole("heading", { name: "Motor Controller", exact: true })).toBeVisible();
  await expect(page.getByText("Motor group 1 status LED", { exact: true })).toBeVisible();
  await page.locator("#dir1").selectOption("arc");
  await expect(page.locator("#arc1-summary")).toBeVisible();
  await page.locator('[data-open-arc="1"]').click();
  await expect(page.locator("#arc-dialog")).toBeVisible();
  await expect(page.locator("#arc-dialog [data-arc-step]")).toHaveCount(6);
  await expect(page.locator("#arc-home")).toBeVisible();
  await expect(page.locator("#arc-dialog-dial .arc-marker.current")).toHaveAttribute("aria-label", /Current angle/);
  await page.locator("#arc-home").click();
  await expect(page.locator("#arc-dialog-position")).toContainText("0° · step 0");
  await expect(page.getByText("Active side", { exact: true })).toHaveCount(0);
  await expect(page.locator("#arcPath1")).toHaveValue("clockwise");
  await expect(page.locator("#arc-dialog .arc-sweep circle")).toHaveAttribute("style", /--arc-sweep:12\./);
  await page.getByRole("button", { name: "Counter-clockwise route" }).click();
  await expect(page.locator("#arcPath1")).toHaveValue("counterclockwise");
  await expect(page.locator("#arc-dialog .arc-sweep circle")).toHaveAttribute("style", /--arc-sweep:87\./);
  await expect(page.getByRole("button", { name: "Use current motor position" })).toBeVisible();
  await page.locator(".arc-node-picker").getByRole("button", { name: "End point" }).click();
  await page.getByRole("button", { name: "+10", exact: true }).click();
  await expect(page.locator("#arc-dialog-end")).toHaveText("522");
  await expect(page.locator("#arc-dialog [data-arc-endpoint=end]")).toHaveAttribute("aria-pressed", "true");
  await expect(page.locator("#arcEnd1")).toHaveValue("522");

  const ring = await page.locator("#arc-dialog-dial .arc-ring").boundingBox();
  const startMarker = await page.locator("#arc-dialog-dial [data-arc-endpoint=start]").boundingBox();
  await page.locator("#arc-dialog-dial [data-arc-endpoint=start]").dispatchEvent("pointerdown", { pointerId: 17, clientX: startMarker.x + startMarker.width / 2, clientY: startMarker.y + startMarker.height / 2 });
  await page.locator("#arc-dialog").dispatchEvent("pointermove", { pointerId: 17, clientX: ring.x + ring.width - 1, clientY: ring.y + ring.height / 2 });
  await page.locator("#arc-dialog").dispatchEvent("pointerup", { pointerId: 17, clientX: ring.x + ring.width - 1, clientY: ring.y + ring.height / 2 });
  await expect(page.locator("#arc-dialog-start")).toHaveText(/10[12][0-9]/);
  await expect(page.locator("#arc-dialog [data-arc-endpoint=start]")).toHaveAttribute("aria-pressed", "true");
  await page.screenshot({ path: "dist/motor-firmware-positioned-arc.png", fullPage: true });
});

test("missing distance sensors cannot be calibrated", async ({ page }) => {
  const portal = pathToFileURL(resolve("firmware/motor_controller/v1.1.35/portal.html")).href + "?preview=1&scenario=none";
  await page.goto(portal, { waitUntil: "load" });
  await expect(page.locator("#sensor1-calibrate")).toBeDisabled();
  await expect(page.locator("#sensor2-calibrate")).toBeDisabled();
});
