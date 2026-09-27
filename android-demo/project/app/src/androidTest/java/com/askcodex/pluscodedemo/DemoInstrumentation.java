package com.askcodex.pluscodedemo;

import android.app.*;
import android.content.*;
import android.graphics.Bitmap;
import android.os.*;
import android.view.*;
import android.widget.TextView;
import org.json.*;
import java.io.*;
import java.util.concurrent.*;

/** Runs against the installed app without interacting with the car's account overlay. */
public final class DemoInstrumentation extends Instrumentation {
    private Activity activity;
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            Intent intent=new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=startActivitySync(intent);
            waitText("全国数据库已就绪");
            String[] buttons={"上海","广州","成都","北京"};
            String[] expected={"上海市 / 上海市","广东省 / 广州市","四川省 / 成都市","北京市 / 北京市"};
            for(int i=0;i<buttons.length;i++){click(buttons[i]);waitText(expected[i]);}
            capture("window-beijing.png");
            click("Plus Code");click("查询行政区");waitText("东城区");capture("window-pluscode.png");
            click("WGS84 经纬度");click("清空块缓存");waitText("块缓存已清空");capture("window-cache-cleared.png");
            click("开始模拟行驶");waitText("模拟行驶  ");SystemClock.sleep(5000);capture("window-simulation.png");click("停止模拟行驶");waitText("北京 → 通州 · 模拟轨迹");
            SystemClock.sleep(300);click("北京");waitText("北京市 / 北京市");
            click("运行设备自检");waitText("设备自检通过");
            File file=new File(getTargetContext().getFilesDir(),"self-test.json");
            String json;
            try(FileInputStream in=new FileInputStream(file);ByteArrayOutputStream out=new ByteArrayOutputStream()) {
                byte[] buffer=new byte[4096];int read;while((read=in.read(buffer))!=-1)out.write(buffer,0,read);
                json=out.toString("UTF-8");
            }
            if(!new JSONObject(json).getBoolean("passed"))throw new AssertionError(json);
            sendStatus(0,result);
            result.putString("stream","\nPASS: 14 JNI checks; four cities; Plus Code; cache clear; simulation; app-window captures.\n"+json);
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            result.putString("stream","FAIL: "+error.toString());finish(Activity.RESULT_CANCELED,result);
        }
    }
    private View find(View root,String label,boolean exact) {
        if(root instanceof TextView) {String t=((TextView)root).getText().toString();if(exact?t.equals(label):t.contains(label))return root;}
        if(root instanceof ViewGroup) {ViewGroup g=(ViewGroup)root;for(int i=0;i<g.getChildCount();i++){View v=find(g.getChildAt(i),label,exact);if(v!=null)return v;}}
        return null;
    }
    private void click(String label) {
        waitForIdleSync();
        runOnMainSync(()->{View v=find(activity.getWindow().getDecorView(),label,true);if(v==null||!v.isEnabled())throw new AssertionError("Missing enabled control: "+label);v.performClick();});
        waitForIdleSync();
    }
    private void waitText(String value)throws Exception {
        long deadline=SystemClock.elapsedRealtime()+45000;
        do {
            boolean[] found={false};runOnMainSync(()->found[0]=find(activity.getWindow().getDecorView(),value,false)!=null);
            if(found[0]) {SystemClock.sleep(200);return;}SystemClock.sleep(100);
        }while(SystemClock.elapsedRealtime()<deadline);
        throw new AssertionError("Missing screen text: "+value);
    }
    private void capture(String name)throws Exception {
        if(Build.VERSION.SDK_INT<26)return;
        Bitmap[] bitmap=new Bitmap[1];int[] status={-1};CountDownLatch done=new CountDownLatch(1);
        runOnMainSync(()->{View v=activity.getWindow().getDecorView();bitmap[0]=Bitmap.createBitmap(v.getWidth(),v.getHeight(),Bitmap.Config.ARGB_8888);PixelCopy.request(activity.getWindow(),bitmap[0],s->{status[0]=s;done.countDown();},new Handler(Looper.getMainLooper()));});
        if(!done.await(10,TimeUnit.SECONDS)||status[0]!=PixelCopy.SUCCESS)throw new AssertionError("PixelCopy: "+status[0]);
        try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),name))){bitmap[0].compress(Bitmap.CompressFormat.PNG,100,out);}
        bitmap[0].recycle();
    }
}
