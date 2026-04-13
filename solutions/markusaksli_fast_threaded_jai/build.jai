#import "Basic";
#import "Compiler";
#import "File";
#import "File_Utilities";
#import "String";
#import "Process";
#import "System";

build :: () {
    target_options := get_build_options(-1);
    set_build_options_dc(.{do_output=false});

    w := compiler_create_workspace("markusaksli_fast_threaded_jai");
    if !w {
        print("Workspace creation failed.\n");
        return;
    }
    target_options.output_executable_name = "markusaksli_fast_threaded_jai";
    print("LLVM setting: %\n", target_options.llvm_options.bitcode_optimization_setting);
    set_build_options(target_options, w);

    add_build_file(tprint("%main.jai", #filepath), w);
}

#run build();
