package com.askcodex.pluscodedemo;

import android.app.*;
import android.os.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import com.askcodex.pluscode.PlusCodeIndex;
import org.json.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

public class MainActivity extends Activity {
    private static final int INK = 0xff182d36, MUTED = 0xff60757e, TEAL = 0xff087f72;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private PlusCodeIndex index;
    private boolean ready, busy, simulating, destroyed, codeMode;
    private int step;
    private TextView state, region, hierarchy, detail, codeResult, timing, metrics, simulationState;
    private EditText latitude, longitude, plusCode;
    private LinearLayout coords;
    private Switch prefetch;
    private Button simulate;
    private final ArrayList<View> actions = new ArrayList<>();
    private String lastJson = "{}";
    private TraceView trace;
    private final double[][] examples = {{39.9042,116.4074},{31.2304,121.4737},{23.1291,113.2644},{30.5728,104.0668}};
    private final Runnable tick = () -> {
        if (!simulating || destroyed) return;
        double t = step / 120.0;
        double lat = 39.9042 + 0.06 * Math.sin(t * Math.PI);
        double lng = 116.4074 + 0.75 * t;
        latitude.setText(String.format(Locale.ROOT,"%.6f",lat));
        longitude.setText(String.format(Locale.ROOT,"%.6f",lng));
        query(false, true);
    };

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        buildUi();
        worker.execute(() -> {
            try {
                File db = DatabaseAsset.prepare(getApplicationContext(), message -> post(() -> state.setText(message)));
                index = new PlusCodeIndex(db.getAbsolutePath(),64,8L*1024*1024);
                String stats = index.cacheStatsJson();
                post(() -> { ready=true; state.setText("全国数据库已就绪 · 离线"); showStats(stats); refreshActions(); query(false,false); });
            } catch (Throwable error) { post(() -> state.setText("初始化失败：" + error.getMessage())); }
        });
    }
    private void post(Runnable action) { ui.post(() -> { if (!destroyed) action.run(); }); }
    private int dp(float n) { return Math.round(n*getResources().getDisplayMetrics().density); }
    private LinearLayout column() { LinearLayout v=new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v; }
    private TextView text(String value,int size,int color) {
        TextView v=new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color); v.setPadding(0,dp(5),0,dp(5)); return v;
    }
    private void title(LinearLayout box,String label) { TextView t=text(label,20,INK); t.setTypeface(null,Typeface.BOLD); box.addView(t); }
    private void label(LinearLayout box,String label) { box.addView(text(label,14,MUTED)); }
    private GradientDrawable background(int color) { GradientDrawable d=new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(6)); return d; }
    private EditText input(String hint,String value,boolean number) {
        EditText e=new EditText(this); e.setTextSize(19); e.setTextColor(INK); e.setHint(hint); e.setContentDescription(hint); e.setSingleLine();
        e.setInputType(number ? InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_FLAG_DECIMAL|InputType.TYPE_NUMBER_FLAG_SIGNED : InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);
        e.setText(value); e.setSelectAllOnFocus(true); e.setMinimumHeight(dp(52)); return e;
    }
    private Button button(String label,LinearLayout parent,Runnable action) {
        Button b=new Button(this); b.setText(label); b.setTextSize(16); b.setAllCaps(false); b.setMinHeight(dp(48));
        parent.addView(b,new LinearLayout.LayoutParams(-1,dp(52))); b.setOnClickListener(v->action.run()); actions.add(b); return b;
    }
    private void buildUi() {
        LinearLayout root=column(); root.setPadding(dp(24),dp(12),dp(24),dp(12)); root.setBackgroundColor(0xfff3f6f7);
        root.setOnApplyWindowInsetsListener((v,insets)-> { v.setPadding(dp(24)+insets.getSystemWindowInsetLeft(),dp(12)+insets.getSystemWindowInsetTop(),dp(24)+insets.getSystemWindowInsetRight(),dp(12)+insets.getSystemWindowInsetBottom()); return insets.consumeSystemWindowInsets(); });
        TextView brand=text("离线行政区  /  Plus Code",28,INK); brand.setTypeface(null,Typeface.BOLD); root.addView(brand);
        state=text("准备全国数据库…",15,TEAL); root.addView(state);
        boolean wide=getResources().getConfiguration().screenWidthDp>=1000;
        LinearLayout body=new LinearLayout(this); body.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
        LinearLayout left=column(), center=column(), right=column();
        LinearLayout[] panels={left,center,right};
        if(wide) {
            root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
            for(LinearLayout p:panels) { ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true); p.setPadding(dp(16),dp(8),dp(20),dp(8)); scroll.addView(p); body.addView(scroll,new LinearLayout.LayoutParams(0,-1,1)); }
        } else {
            ScrollView scroll=new ScrollView(this); root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1)); scroll.addView(body);
            for(LinearLayout p:panels) { p.setPadding(0,dp(14),0,dp(18)); body.addView(p,new LinearLayout.LayoutParams(-1,-2)); }
        }
        title(left,"查询位置");
        RadioGroup modes=new RadioGroup(this); modes.setOrientation(LinearLayout.HORIZONTAL);
        RadioButton byCoordinates=new RadioButton(this), byCode=new RadioButton(this);
        byCoordinates.setText("WGS84 经纬度"); byCode.setText("Plus Code"); byCoordinates.setId(View.generateViewId()); byCode.setId(View.generateViewId());
        modes.addView(byCoordinates); modes.addView(byCode); byCoordinates.setChecked(true); left.addView(modes);
        actions.add(byCoordinates); actions.add(byCode);
        coords=new LinearLayout(this); left.addView(coords);
        LinearLayout latBox=column(),lngBox=column(); latBox.setPadding(0,0,dp(10),0);
        coords.addView(latBox,new LinearLayout.LayoutParams(0,-2,1));coords.addView(lngBox,new LinearLayout.LayoutParams(0,-2,1));
        label(latBox,"纬度 Latitude"); latitude=input("纬度",getPreferences(0).getString("lat","39.904200"),true); latBox.addView(latitude);
        label(lngBox,"经度 Longitude"); longitude=input("经度",getPreferences(0).getString("lng","116.407400"),true); lngBox.addView(longitude);
        plusCode=input("完整 Plus Code","8PFRWC34+MXJ",false); left.addView(plusCode); plusCode.setVisibility(View.GONE);
        modes.setOnCheckedChangeListener((g,id)->{ codeMode=id==byCode.getId(); coords.setVisibility(codeMode?View.GONE:View.VISIBLE); plusCode.setVisibility(codeMode?View.VISIBLE:View.GONE); });
        Button query=button("查询行政区",left,()->query(codeMode,false)); query.setTextColor(Color.WHITE); query.setBackground(background(TEAL));
        label(left,"示例位置");
        LinearLayout choices=new LinearLayout(this); left.addView(choices);
        String[] names={"北京","上海","广州","成都"};
        for(int i=0;i<names.length;i++) { final int n=i; Button b=new Button(this); b.setText(names[i]); b.setTextSize(15); choices.addView(b,new LinearLayout.LayoutParams(0,dp(50),1)); actions.add(b); b.setOnClickListener(v->{byCoordinates.setChecked(true); latitude.setText(""+examples[n][0]); longitude.setText(""+examples[n][1]); query(false,false);}); }
        prefetch=new Switch(this); prefetch.setText("周边预热"); prefetch.setChecked(true); prefetch.setTextSize(16); left.addView(prefetch,new LinearLayout.LayoutParams(-1,dp(48)));
        simulate=button("开始模拟行驶",left,()->{ if(simulating) stopSimulation(); else { byCoordinates.setChecked(true); simulating=true; step=0; trace.reset(); refreshActions(); tick.run(); } });
        simulationState=text("北京 → 通州 · 模拟轨迹",14,MUTED); left.addView(simulationState);
        title(center,"行政区结果");
        region=text("等待查询",30,INK); region.setTypeface(null,Typeface.BOLD); center.addView(region);
        hierarchy=text("",19,TEAL); center.addView(hierarchy);
        codeResult=text("",18,INK); codeResult.setTypeface(Typeface.MONOSPACE); codeResult.setTextIsSelectable(true); center.addView(codeResult);
        detail=text("",16,MUTED); center.addView(detail);
        timing=text("",15,TEAL); center.addView(timing);
        trace=new TraceView(); center.addView(trace,new LinearLayout.LayoutParams(-1,dp(145)));
        title(right,"运行状态");
        metrics=text("等待原生索引…",18,INK); metrics.setLineSpacing(dp(5),1); right.addView(metrics);
        button("清空块缓存",right,()->runTask(()->{index.clearCache(); return index.cacheStatsJson();},s->{showStats(s);state.setText("块缓存已清空");}));
        button("查看结果 JSON",right,()->jsonDialog("查询结果",lastJson));
        button("运行设备自检",right,this::selfTest);
        root.setFocusableInTouchMode(true); setContentView(root); root.requestFocus(); refreshActions();
    }
    private void refreshActions() {
        for(View v:actions) v.setEnabled(ready&&!busy&&!simulating);
        simulate.setEnabled(ready&&(simulating||!busy)); simulate.setText(simulating?"停止模拟行驶":"开始模拟行驶");
        latitude.setEnabled(!simulating&&!busy); longitude.setEnabled(!simulating&&!busy); plusCode.setEnabled(!simulating&&!busy);
    }
    private interface Task { String run() throws Exception; }
    private interface Success { void accept(String value) throws Exception; }
    private void runTask(Task task,Success success) {
        if(!ready||busy) return;
        busy=true; refreshActions();
        worker.execute(()->{try {String value=task.run(); post(()->{busy=false;try{success.accept(value);}catch(Exception e){showError(e);} refreshActions();});}
            catch(Exception e){post(()->{busy=false;showError(e);refreshActions();});}});
    }
    private void query(boolean code,boolean moving) {
        if(!ready||busy) return;
        final double lat,lng;
        final String inputCode=plusCode.getText().toString().trim();
        try {
            lat=code?0:Double.parseDouble(latitude.getText().toString().trim()); lng=code?0:Double.parseDouble(longitude.getText().toString().trim());
            if(!code&&(Double.isNaN(lat)||Double.isInfinite(lat)||Double.isNaN(lng)||Double.isInfinite(lng)||lat < -90||lat > 90||lng < -180||lng > 180)) throw new IllegalArgumentException("纬度须为 -90～90，经度须为 -180～180");
        } catch(Exception e) {showError(new IllegalArgumentException("请输入有效的 WGS84 经纬度"));return;}
        ((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(latitude.getWindowToken(),0);
        if(!code) getPreferences(0).edit().putString("lat",latitude.getText().toString()).putString("lng",longitude.getText().toString()).apply();
        final boolean warm=prefetch.isChecked()&&!code;
        busy=true;refreshActions();
        worker.execute(()->{
            try {
                long start=System.nanoTime();
                PlusCodeIndex.Result result=code?index.lookup(inputCode):index.lookupLatLng(lat,lng);
                double ms=(System.nanoTime()-start)/1e6;
                if(warm) index.prefetchNearby(lat,lng,1,16);
                String stats=index.cacheStatsJson();
                post(()->{busy=false;showResult(result,ms);showStats(stats);
                    if(!code) trace.point(lat,lng,moving);
                    if(moving&&simulating) {step++;simulationState.setText("模拟行驶  "+step+" / 121 · "+String.format(Locale.ROOT,"%.4f, %.4f",lat,lng)); if(step<=120) ui.postDelayed(tick,650); else stopSimulation();}
                    refreshActions();});
            } catch(Exception e){post(()->{busy=false;stopSimulation();showError(e);refreshActions();});}
        });
    }
    private String value(String s) {return s==null||s.isEmpty()?"—":s;}
    private void showResult(PlusCodeIndex.Result r,double ms) {
        lastJson=r.json; state.setText("全国数据库已就绪 · 离线");
        region.setText(r.isMatched()?value(r.admin.county==null?r.admin.province:r.admin.county):r.isAmbiguous()?"跨行政区网格":"未覆盖位置");
        hierarchy.setText(r.isMatched()?value(r.admin.province)+" / "+value(r.admin.city):r.isAmbiguous()?"候选行政区："+r.sampledCandidates.size():"outside_coverage");
        codeResult.setText("Plus Code   "+value(r.plusCode));
        detail.setText((r.isMatched()?"行政区码  "+value(r.admin.countyCode)+"\n":"")+"命中网格  "+value(r.matchedPlusCode)+"\n边界网格  "+(r.boundaryCell?"是":"否")+"    源数据重叠  "+(r.sourceOverlap?"是":"否"));
        timing.setText(String.format(Locale.ROOT,"JNI 查询  %.3f ms",ms));
    }
    private void showStats(String raw) {
        try {JSONObject s=new JSONObject(raw); double hits=s.getDouble("hits"), misses=s.getDouble("misses");
            metrics.setText(String.format(Locale.ROOT,"全国文件     210.34 MiB\n块缓存         %.1f / 8192 KiB\n缓存块         %d / 64\n命中 / 未命中   %.0f / %.0f\n累计加载       %d 块\n累计读取       %.1f KiB\nLRU 淘汰       %d 块\n进程 PSS       %.1f MiB",s.getDouble("cached_bytes")/1024,s.getInt("cached_tiles"),hits,misses,s.getInt("loads"),s.getDouble("compressed_bytes_read")/1024,s.getInt("evictions"),Debug.getPss()/1024.0));
        }catch(Exception e){metrics.setText(raw);}
    }
    private void showError(Exception e) {state.setText("查询失败："+value(e.getMessage())); region.setText("输入或查询异常");hierarchy.setText("");detail.setText("");codeResult.setText("");timing.setText("");lastJson="{}";}
    private void stopSimulation() {
        simulating=false;
        ui.removeCallbacks(tick);
        if(simulationState!=null) simulationState.setText("北京 → 通州 · 模拟轨迹");
        refreshActions();
    }
    private void jsonDialog(String title,String json) {
        String formatted=json;try {formatted=new JSONObject(json).toString(2);}catch(Exception ignored){}
        final String content=formatted;
        TextView t=text(content,14,INK); t.setTypeface(Typeface.MONOSPACE);t.setTextIsSelectable(true);t.setPadding(dp(20),dp(12),dp(20),dp(12));
        ScrollView scroll=new ScrollView(this);scroll.addView(t);
        new AlertDialog.Builder(this).setTitle(title).setView(scroll).setPositiveButton("关闭",null).setNeutralButton("复制",(d,w)->{((android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText(title,content));}).show();
    }
    private void selfTest() {
        runTask(()->{
            JSONArray cases=new JSONArray();
            try(PlusCodeIndex test=new PlusCodeIndex(new File(getFilesDir(),"pluscode-v2-"+DatabaseAsset.SHA.substring(0,12)+".sqlite").getAbsolutePath(),64,8L*1024*1024)) {
                check(cases,"open_without_loading_blocks",new JSONObject(test.cacheStatsJson()).getInt("loads")==0);
                String[] provinces={"北京市","上海市","广东省","四川省"};
                for(int i=0;i<examples.length;i++) {PlusCodeIndex.Result r=test.lookupLatLng(examples[i][0],examples[i][1]);check(cases,"coordinate_"+i,r.isMatched()&&provinces[i].equals(r.admin.province));check(cases,"pluscode_roundtrip_"+i,test.lookup(r.plusCode).json.equals(r.json));}
                check(cases,"outside_coverage",!test.lookupLatLng(0,0).isMatched());
                boolean invalid=false;try{test.lookupLatLng(91,0);}catch(IllegalArgumentException expected){invalid=true;}check(cases,"invalid_coordinate",invalid);
                invalid=false;try{test.lookup("INVALID");}catch(IllegalArgumentException expected){invalid=true;}check(cases,"invalid_pluscode",invalid);
                test.prefetchNearby(39.9042,116.4074,1,16);JSONObject s=new JSONObject(test.cacheStatsJson());check(cases,"bounded_prefetch",s.getInt("cached_tiles")<=64&&s.getLong("cached_bytes")<=8L*1024*1024);
                test.clearCache();check(cases,"clear_cache",new JSONObject(test.cacheStatsJson()).getInt("cached_tiles")==0);
            }
            boolean passed=true;for(int i=0;i<cases.length();i++)passed&=cases.getJSONObject(i).getBoolean("passed");
            String report=new JSONObject().put("passed",passed).put("device",Build.MODEL).put("sdk",Build.VERSION.SDK_INT).put("database_sha256",DatabaseAsset.SHA).put("cases",cases).toString(2);
            try(FileOutputStream out=openFileOutput("self-test.json",MODE_PRIVATE)){out.write(report.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
            return report;
        },report->{state.setText(new JSONObject(report).getBoolean("passed")?"设备自检通过 · 14 项":"设备自检失败");jsonDialog("设备自检",report);});
    }
    private void check(JSONArray cases,String name,boolean ok)throws JSONException {cases.put(new JSONObject().put("name",name).put("passed",ok));}
    @Override protected void onPause(){super.onPause();stopSimulation();}
    @Override protected void onDestroy(){destroyed=true;ui.removeCallbacksAndMessages(null);worker.execute(()->{if(index!=null)index.close();});worker.shutdown();super.onDestroy();}

    private class TraceView extends View {
        private final Paint paint=new Paint(3);
        private final ArrayList<double[]> points=new ArrayList<>();
        TraceView(){super(MainActivity.this);setContentDescription("WGS84 模拟轨迹示意");}
        void reset(){points.clear();invalidate();}
        void point(double lat,double lng,boolean moving){if(!moving)points.clear();points.add(new double[]{lat,lng});if(points.size()>121)points.remove(0);invalidate();}
        @Override protected void onDraw(Canvas c){
            super.onDraw(c);float w=getWidth(),h=getHeight();c.drawColor(0xffe8eef0);
            paint.setStrokeWidth(dp(1));paint.setColor(0xffcfdcdf);
            for(int i=1;i<8;i++)c.drawLine(w*i/8,0,w*i/8,h,paint);
            for(int i=1;i<4;i++)c.drawLine(0,h*i/4,w,h*i/4,paint);
            paint.setTextSize(dp(13));paint.setColor(MUTED);c.drawText(points.size()>1?"模拟轨迹 · WGS84":"WGS84 位置",dp(12),dp(22),paint);
            if(points.isEmpty())return;
            double minLat=points.get(0)[0]-.02,maxLat=minLat+.04,minLng=points.get(0)[1]-.03,maxLng=minLng+.06;
            if(points.size()>1){minLat=39.88;maxLat=40.00;minLng=116.38;maxLng=117.20;}
            float prevX=0,prevY=0;paint.setStrokeWidth(dp(3));paint.setColor(TEAL);
            for(int i=0;i<points.size();i++){double[] p=points.get(i);float x=dp(20)+(float)((p[1]-minLng)/(maxLng-minLng))*(w-dp(40));float y=h-dp(16)-(float)((p[0]-minLat)/(maxLat-minLat))*(h-dp(50));if(i>0)c.drawLine(prevX,prevY,x,y,paint);prevX=x;prevY=y;}
            paint.setColor(0xffdd9932);c.drawCircle(prevX,prevY,dp(7),paint);paint.setColor(Color.WHITE);c.drawCircle(prevX,prevY,dp(3),paint);
        }
    }
}
