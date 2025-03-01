// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.util;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.os.Build;
import android.text.TextUtils;
import android.util.Pair;
import android.view.View;

import org.junit.rules.TestWatcher;
import org.junit.runner.Description;

import org.chromium.base.CommandLine;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.ui.UiUtils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.LinkedList;
import java.util.List;
import java.util.concurrent.Callable;

/**
 * A TestRule for creating Render Tests. An exception will be thrown after the test method completes
 * if the test fails.
 *
 * <pre>
 * {@code
 *
 * @RunWith(ChromeJUnit4ClassRunner.class)
 * @CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
 * public class MyTest {
 *     @Rule
 *     public RenderTestRule mRenderTestRule =
 *             new RenderTestRule("chrome/test/data/android/render_tests");
 *
 *     @Test
 *     public void testViewAppearance() {
 *         // Setup the UI.
 *         ...
 *
 *         // Render UI Elements.
 *         mRenderTestRule.render(bigWidgetView, "big_widget");
 *         mRenderTestRule.render(smallWidgetView, "small_widget");
 *     }
 * }
 *
 * }
 * </pre>
 */
public class RenderTestRule extends TestWatcher {
    private static final String TAG = "RenderTest";

    private static final String DIFF_FOLDER_RELATIVE = "/diffs";
    private static final String FAILURE_FOLDER_RELATIVE = "/failures";
    private static final String GOLDEN_FOLDER_RELATIVE = "/goldens";

    /**
     * This is a list of model-SDK version identifiers for devices we maintain golden images for.
     * If render tests are being run on a device of a model-sdk on this list, goldens should exist.
     */
    // TODO(peconn): Add "Nexus_5X-23" once it's run on CQ - https://crbug.com/731759.
    private static final String[] RENDER_TEST_MODEL_SDK_PAIRS = {"Nexus_5-19"};

    /** How many pixels can be different in an image before counting the images as different. */
    private static final int PIXEL_DIFF_THRESHOLD = 0;

    private enum ComparisonResult { MATCH, MISMATCH, GOLDEN_NOT_FOUND }

    // State for a test class.
    private final String mOutputDirectory;
    private final String mGoldenFolder;

    // State for a test method.
    private String mTestClassName;
    private List<String> mMismatchIds = new LinkedList<>();
    private List<String> mGoldenMissingIds = new LinkedList<>();
    /** Parameterized tests have a prefix inserted at the front of the test description. */
    private String mVariantPrefix;

    /**
     * An exception thrown after a Render Test if images do not match the goldens or goldens are
     * missing on a render test device.
     */
    public static class RenderTestException extends RuntimeException {
        public RenderTestException(String message) {
            super(message);
        }
    }

    public RenderTestRule(String goldenFolder) {
        mGoldenFolder = goldenFolder;
        // The output directory can be overridden with the --render-test-output-dir command.
        mOutputDirectory =
                CommandLine.getInstance().getSwitchValue("render-test-output-dir", goldenFolder);
    }

    @Override
    protected void starting(Description desc) {
        // desc.getClassName() gets the fully qualified name.
        mTestClassName = desc.getTestClass().getSimpleName();

        mMismatchIds.clear();
        mGoldenMissingIds.clear();
    }

    /**
     * Renders the |view| and compares it to the golden view with the |id|. The RenderTestRule will
     * throw an exception after the test method has completed if the view does not match the
     * golden or if a golden is missing on a device it should be present (see
     * {@link RenderTestRule#RENDER_TEST_MODEL_SDK_PAIRS}).
     *
     * @throws IOException if the rendered image cannot be saved to the device.
     */
    public void render(final View view, String id) throws IOException {
        Bitmap testBitmap = ThreadUtils.runOnUiThreadBlockingNoException(new Callable<Bitmap>() {
            @Override
            public Bitmap call() throws Exception {
                int height = view.getMeasuredHeight();
                int width = view.getMeasuredWidth();
                if (height <= 0 || width <= 0) {
                    throw new IllegalStateException(
                            "Invalid view dimensions: " + width + "x" + height);
                }

                return UiUtils.generateScaledScreenshot(view, 0, Bitmap.Config.ARGB_8888);
            }
        });

        String filename = imageName(mTestClassName, mVariantPrefix, id);

        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inPreferredConfig = testBitmap.getConfig();
        File goldenFile = createGoldenPath(filename);
        Bitmap goldenBitmap = BitmapFactory.decodeFile(goldenFile.getAbsolutePath(), options);

        Pair<ComparisonResult, Bitmap> result = compareBitmapToGolden(testBitmap, goldenBitmap);
        Log.i(TAG, "RenderTest %s %s", id, result.first.toString());

        // Save the result and any interesting images.
        switch (result.first) {
            case MATCH:
                // We don't do anything with the matches.
                break;
            case GOLDEN_NOT_FOUND:
                mGoldenMissingIds.add(id);

                saveBitmap(testBitmap, createOutputPath(FAILURE_FOLDER_RELATIVE, filename));
                break;
            case MISMATCH:
                mMismatchIds.add(id);

                saveBitmap(testBitmap, createOutputPath(FAILURE_FOLDER_RELATIVE, filename));
                saveBitmap(goldenBitmap, createOutputPath(GOLDEN_FOLDER_RELATIVE, filename));
                saveBitmap(result.second, createOutputPath(DIFF_FOLDER_RELATIVE, filename));
                break;
        }
    }

    @Override
    protected void finished(Description desc) {
        if (!onRenderTestDevice() && !mGoldenMissingIds.isEmpty()) {
            Log.d(TAG, "RenderTest missing goldens, but we are not on a render test device.");
            mGoldenMissingIds.clear();
        }

        if (mGoldenMissingIds.isEmpty() && mMismatchIds.isEmpty()) {
            // Everything passed!
            return;
        }

        StringBuilder sb = new StringBuilder();
        if (!mGoldenMissingIds.isEmpty()) {
            sb.append("RenderTest Goldens missing for: ");
            sb.append(TextUtils.join(", ", mGoldenMissingIds));
        }

        if (!mMismatchIds.isEmpty()) {
            if (sb.length() != 0) sb.append(" ");
            sb.append("RenderTest Mismatches for: ");
            sb.append(TextUtils.join(", ", mMismatchIds));
        }

        throw new RenderTestException(sb.toString());
    }

    /**
     * Returns whether goldens should exist for the current device.
     */
    private static boolean onRenderTestDevice() {
        for (String model : RENDER_TEST_MODEL_SDK_PAIRS) {
            if (model.equals(modelSdkIdentifier())) return true;
        }
        return false;
    }

    /**
     * Sets a string that will be inserted at the start of the description in the golden image name.
     * This is used to create goldens for multiple different variants of the UI.
     */
    public void setVariantPrefix(String variantPrefix) {
        mVariantPrefix = variantPrefix;
    }

    /**
     * Creates an image name combining the image description with details about the device
     * (eg model, current orientation).
     *
     * This function must be kept in sync with |RE_RENDER_IMAGE_NAME| from
     * src/build/android/pylib/local/device/local_device_instrumentation_test_run.py.
     */
    private static String imageName(String testClass, String variantPrefix, String desc) {
        if (!TextUtils.isEmpty(variantPrefix)) {
            desc = variantPrefix + "-" + desc;
        }

        return String.format("%s.%s.%s.png", testClass, desc, modelSdkIdentifier());
    }

    /**
     * Returns a string encoding the device model and sdk. It is used to identify device goldens.
     */
    private static String modelSdkIdentifier() {
        return Build.MODEL.replace(' ', '_') + "-" + Build.VERSION.SDK_INT;
    }

    /**
     * Saves a the given |bitmap| to the |file|.
     */
    private static void saveBitmap(Bitmap bitmap, File file) throws IOException {
        FileOutputStream out = new FileOutputStream(file);
        try {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        } finally {
            out.close();
        }
    }

    /**
     * Convenience method to create a File pointing to |filename| in |mGoldenFolder|.
     */
    private File createGoldenPath(String filename) throws IOException {
        return createPath(UrlUtils.getIsolatedTestFilePath(mGoldenFolder), filename);
    }

    /**
     * Convenience method to create a File pointing to |filename| in the |subfolder| in
     * |mOutputDirectory|.
     */
    private File createOutputPath(String subfolder, String filename) throws IOException {
        return createPath(mOutputDirectory + subfolder, filename);
    }

    private static File createPath(String folder, String filename) throws IOException {
        File path = new File(folder);
        if (!path.exists()) {
            if (!path.mkdirs()) {
                throw new IOException("Could not create " + path.getAbsolutePath());
            }
        }
        return new File(path + "/" + filename);
    }

    /**
     * Compares two Bitmaps.
     * @return A pair of ComparisonResult and Bitmap. If the ComparisonResult is MISMATCH or MATCH,
     *         the Bitmap will be a generated pixel-by-pixel difference.
     */
    private static Pair<ComparisonResult, Bitmap> compareBitmapToGolden(
            Bitmap render, Bitmap golden) {
        if (golden == null) return Pair.create(ComparisonResult.GOLDEN_NOT_FOUND, null);

        Bitmap diff = Bitmap.createBitmap(Math.max(render.getWidth(), golden.getWidth()),
                Math.max(render.getHeight(), golden.getHeight()), render.getConfig());

        int maxWidth = Math.max(render.getWidth(), golden.getWidth());
        int maxHeight = Math.max(render.getHeight(), golden.getHeight());
        int minWidth = Math.min(render.getWidth(), golden.getWidth());
        int minHeight = Math.min(render.getHeight(), golden.getHeight());

        int diffPixelsCount = comparePixels(render, golden, diff, 0, 0, minWidth, 0, minHeight)
                + compareSizes(diff, minWidth, maxWidth, minHeight, maxHeight);

        if (diffPixelsCount > PIXEL_DIFF_THRESHOLD) {
            return Pair.create(ComparisonResult.MISMATCH, diff);
        }
        return Pair.create(ComparisonResult.MATCH, diff);
    }

    /**
     * Compares two bitmaps pixel-wise.
     *
     * @param testImage Bitmap of test image.
     *
     * @param goldenImage Bitmap of golden image.
     *
     * @param diffImage This is an output argument. Function will set pixels in the |diffImage| to
     * either transparent or red depending on whether that pixel differed in the golden and test
     * bitmaps. diffImage should have its width and height be the max width and height of the
     * golden and test bitmaps.
     *
     * @param diffThreshold Threshold for when to consider two color values as different. These
     * values are 8 bit (256) so this threshold value should be in range 0-256.
     *
     * @param startWidth Start x-coord to start diffing the Bitmaps.
     *
     * @param endWidth End x-coord to start diffing the Bitmaps.
     *
     * @param startHeight Start y-coord to start diffing the Bitmaps.
     *
     * @param endHeight End x-coord to start diffing the Bitmaps.
     *
     * @return Returns number of pixels that differ between |goldenImage| and |testImage|
     */
    private static int comparePixels(Bitmap testImage, Bitmap goldenImage, Bitmap diffImage,
            int diffThreshold, int startWidth, int endWidth, int startHeight, int endHeight) {
        int diffPixels = 0;

        for (int x = startWidth; x < endWidth; x++) {
            for (int y = startHeight; y < endHeight; y++) {
                int goldenColor = goldenImage.getPixel(x, y);
                int testColor = testImage.getPixel(x, y);

                int redDiff = Math.abs(Color.red(goldenColor) - Color.red(testColor));
                int blueDiff = Math.abs(Color.green(goldenColor) - Color.green(testColor));
                int greenDiff = Math.abs(Color.blue(goldenColor) - Color.blue(testColor));
                int alphaDiff = Math.abs(Color.alpha(goldenColor) - Color.alpha(testColor));

                if (redDiff > diffThreshold || blueDiff > diffThreshold || greenDiff > diffThreshold
                        || alphaDiff > diffThreshold) {
                    diffPixels++;
                    diffImage.setPixel(x, y, Color.RED);
                } else {
                    diffImage.setPixel(x, y, Color.TRANSPARENT);
                }
            }
        }
        return diffPixels;
    }

    /**
     * Compares two bitmaps size.
     *
     * @param diffImage This is an output argument. Function will set pixels in the |diffImage| to
     * either transparent or red depending on whether that pixel coordinate occurs in the
     * dimensions of the golden and not the test bitmap or vice-versa.
     *
     * @param minWidth Min width of golden and test bitmaps.
     *
     * @param maxWidth Max width of golden and test bitmaps.
     *
     * @param minHeight Min height of golden and test bitmaps.
     *
     * @param maxHeight Max height of golden and test bitmaps.
     *
     * @return Returns number of pixels that differ between |goldenImage| and |testImage| due to
     * their size.
     */
    private static int compareSizes(
            Bitmap diffImage, int minWidth, int maxWidth, int minHeight, int maxHeight) {
        int diffPixels = 0;
        if (maxWidth > minWidth) {
            for (int x = minWidth; x < maxWidth; x++) {
                for (int y = 0; y < maxHeight; y++) {
                    diffImage.setPixel(x, y, Color.RED);
                }
            }
            diffPixels += (maxWidth - minWidth) * maxHeight;
        }
        if (maxHeight > minHeight) {
            for (int x = 0; x < maxWidth; x++) {
                for (int y = minHeight; y < maxHeight; y++) {
                    diffImage.setPixel(x, y, Color.RED);
                }
            }
            diffPixels += (maxHeight - minHeight) * minWidth;
        }
        return diffPixels;
    }
}
